/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Notepad Next is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Notepad Next.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef REMOTE_SSHSESSIONWORKER_H
#define REMOTE_SSHSESSIONWORKER_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>

#include "ISshTransport.h"
#include "SshProfile.h"

class QSocketNotifier;

namespace remote {

// One decoded remote directory entry, delivered to the UI thread for the file
// tree / readdir callback. Decoded from ISshTransport::SftpDirEntry on the
// worker thread (UTF-8 name, the size/mtime/isDir the tree needs) so the UI
// never touches a raw SFTP attribute struct. "." / ".." are filtered out on the
// worker, matching local readdir (QDir::NoDotAndDotDot).
struct RemoteDirEntry
{
    QString name;       // UTF-8-decoded filename, no path
    bool    isDir = false;
    qint64  size = 0;
    qint64  mtimeSecs = 0; // seconds since epoch (SFTP server clock), 0 if absent
};

// Lives on a dedicated worker QThread. Owns the LIBSSH2_SESSION via an injected
// ISshTransport and is the ONLY place libssh2 is ever touched. Drives:
//   - the connect/auth state machine (D2),
//   - the round-robin read pump (D3),
//   - the write path with the write-notifier invariant (D4),
//   - the FIFO channel-open queue with a cap (D5),
//   - connection-loss detection + ordered cleanup (D8).
//
// The UI-thread facade (SshConnection) talks to it ONLY via queued
// signals/slots — no locks. Tests construct it directly on the test thread,
// inject a FakeSshTransport, and call pumpForTest() to drive the pump without a
// real QSocketNotifier.
class SshSessionWorker : public QObject
{
    Q_OBJECT

public:
    enum class State
    {
        Idle,
        ConnectingSocket,
        Handshaking,
        AwaitingHostKey, // emitted hostKeyReceived; auth gated on accept/reject
        Authenticating,
        Ready,
        Disconnected,
        Failed,
    };
    Q_ENUM(State)

    struct ConnectParams
    {
        QString host;
        int port = 22;
        QString username;
        SshProfile::AuthMethod authMethod = SshProfile::AuthMethod::Agent;
        QString keyPath;
        // Secret providers invoked ON THE WORKER THREAD during auth so the UI
        // thread is never blocked on a keychain syscall (spec: "Secret fetched
        // off the UI thread"). Empty/unset → empty secret.
        std::function<QString()> passwordProvider;
        std::function<QString()> passphraseProvider;
    };

    // Takes ownership of the transport. Default cap = 10 concurrent channels.
    explicit SshSessionWorker(std::unique_ptr<ISshTransport> transport,
                              int channelCap = 10,
                              QObject *parent = nullptr);
    ~SshSessionWorker() override;

    State state() const { return m_state; }

    // --- test hooks ----------------------------------------------------------
    // Run exactly one bounded pump sweep (advance connect/auth, then if Ready
    // service channel setup + reads + writes). Returns nothing; observe via
    // signals + the accessors below.
    void pumpForTest() { pump(); }
    // Drive exactly one bounded SFTP service sweep (mirrors pumpForTest for the
    // D1 SFTP engine): advance the head SFTP op as far as it can go this pass,
    // emitting its *Done signal on completion. Tests script the fake transport
    // then call this to step partial-read / EAGAIN reassembly deterministically.
    void serviceSftpForTest() { serviceSftp(); }
    // Drive exactly one bounded exec service sweep (mirrors serviceSftpForTest
    // for the D6 exec engine): advance every in-flight exec channel as far as
    // it can go this pass, emitting exec*Chunk / execDone as streams progress.
    void serviceExecForTest() { serviceExec(); }
    bool writeNotifierEnabledForTest() const { return m_writeNotifierWanted; }
    int liveChannelCountForTest() const { return liveChannelCount(); }
    int queuedChannelCountForTest() const { return m_openQueue.size(); }

public slots:
    // All posted from the UI thread (queued) in production; called directly in
    // tests. startConnect captures params; in production it also wires the
    // socket notifiers once the fd is valid.
    void startConnect(const remote::SshSessionWorker::ConnectParams &params);
    void acceptHostKey();
    void rejectHostKey();

    // logicalId is minted by the UI-thread facade and is stable for the
    // channel's whole life. wantPty + term/cols/rows + command describe the
    // channel setup (PTY shell for the remote terminal).
    void requestOpenChannel(int logicalId, bool wantPty, const QByteArray &term,
                            int cols, int rows, const QString &command);
    void requestResize(int logicalId, int cols, int rows);
    void requestWrite(int logicalId, const QByteArray &bytes);
    void requestCloseChannel(int logicalId);
    void requestDisconnect();

    // --- SFTP request slots (D1) ---------------------------------------------
    // Posted from the UI thread (queued) by SshConnection on behalf of
    // RemoteFsBackend; called directly in tests. reqId is minted UI-side and is
    // echoed back in the matching *Done signal so the backend can resolve the
    // right callback. All four enqueue an op on the single reused SFTP session
    // (one sftpInit, never per-op) and drive it as far as it can go now; EAGAIN
    // re-arms on the next socket edge (production) or serviceSftpForTest (tests).
    void requestSftpRead(quint64 reqId, const QString &path);
    void requestSftpWrite(quint64 reqId, const QString &path, const QByteArray &data);
    void requestSftpStat(quint64 reqId, const QString &path);
    void requestSftpReaddir(quint64 reqId, const QString &path);

    // --- exec request slots (D6/D8) ------------------------------------------
    // Posted from the UI thread (queued) by SshConnection on behalf of
    // RemoteGitProcessRunner (and, later, the ACP exec transport); called
    // directly in tests. reqId is minted UI-side and echoed in the matching
    // exec* result signals. Each requestExec opens its OWN non-PTY exec channel
    // (multiplexed on the live session), runs `command`, feeds stdinPayload (if
    // any), streams stdout/stderr back, and reports the channel exit status —
    // independent of and concurrent with other exec ops and the PTY channels.
    // requestExecCancel closes the channel for an in-flight op without emitting
    // a result (the runner already resolved its callback).
    void requestExec(quint64 reqId, const QString &command, const QByteArray &stdinPayload);
    void requestExecCancel(quint64 reqId);
    // Append bytes to an in-flight exec op's stdin (D8). The one-shot
    // stdinPayload of requestExec covers the git runner (feed once, then read to
    // EOF); a long-lived ACP agent session instead streams JSON-RPC frames over
    // the channel's whole life, so the ACP exec transport posts each frame here.
    // Bytes are buffered and drained on the next service pass (EAGAIN re-arms the
    // write notifier). No EOF — stdin stays open until the op finishes/cancels.
    void requestExecWrite(quint64 reqId, const QByteArray &bytes);

signals:
    void stateChanged(remote::SshSessionWorker::State state);
    void hostKeyReceived(const QString &fingerprint, const QByteArray &key);
    void authFailed(const QString &reason);
    void connected();
    void channelOpened(int logicalId);
    void channelOpenFailed(int logicalId, const QString &reason);
    void dataReady(int logicalId, const QByteArray &bytes);
    void channelClosed(int logicalId, int exitStatus);
    // Connection lost / disconnected with a human reason.
    void disconnected(const QString &reason);

    // --- SFTP result signals (D1) --------------------------------------------
    // Emitted on the worker thread; relayed queued to the UI thread by
    // SshConnection. `ok` is the I/O-success flag; `error` is a human reason on
    // failure. statDone's `exists` is false (with ok=true) for an absent path —
    // a missing file is not an I/O error, matching the local QFileInfo backend.
    void sftpReadDone(quint64 reqId, bool ok, const QByteArray &data, const QString &error);
    void sftpWriteDone(quint64 reqId, bool ok, const QString &error);
    void sftpStatDone(quint64 reqId, bool ok, bool exists, bool isDir,
                      qint64 size, qint64 mtimeSecs, const QString &error);
    void sftpReaddirDone(quint64 reqId, bool ok,
                         const QList<remote::RemoteDirEntry> &entries, const QString &error);

    // --- exec result signals (D6/D8) -----------------------------------------
    // Emitted on the worker thread; relayed queued to the UI thread by
    // SshConnection. stdout/stderr chunks arrive as they are read off the
    // channel (the runner accumulates them); execDone carries the channel exit
    // status once the command's streams hit EOF (or -1 on connection loss).
    void execStdoutChunk(quint64 reqId, const QByteArray &chunk);
    void execStderrChunk(quint64 reqId, const QByteArray &chunk);
    void execDone(quint64 reqId, int exitStatus);

private slots:
    void onSocketActivity();

private:
    enum class ChPhase
    {
        Queued,    // waiting for a slot (counts against queue, not the cap)
        Opening,   // openChannel in flight (counts against the cap)
        NeedPty,   // channel open; requestPty pending
        NeedExec,  // pty done (or skipped); execOrShell pending
        Open,      // fully set up; in the read/write rotation
        Closed,    // terminal; pending removal
    };

    struct Channel
    {
        int logicalId = -1;
        int transportId = -1;
        ChPhase phase = ChPhase::Queued;
        bool wantPty = false;
        QByteArray term;
        int cols = 80;
        int rows = 24;
        QString command;     // empty → interactive shell
        QByteArray pending;  // unsent write bytes (move-appended; no per-byte alloc)
    };

    void setState(State s);
    void pump();                 // single bounded sweep (D3 + D4 + setup)
    void advanceConnect();       // socket→handshake→hostkey→auth→ready
    void advanceChannelSetup();  // open/pty/exec for not-yet-Open channels
    void readSweep();            // round-robin reads (D3)
    void flushPendingWrites();   // write path + notifier invariant (D4)
    void tryStartQueued();       // FIFO dequeue while under cap (D5)
    void enterConnectionLost(const QString &reason); // D8
    void finishChannel(int logicalId, int exitStatus);

    int liveChannelCount() const;        // Opening..Open (counts against cap)
    Channel *channelByTransportId(int transportId);

    void setupNotifiers();       // production only (real fd)
    void teardownNotifiers();
    void setWriteNotifierEnabled(bool on);

    // --- SFTP engine (D1): one reused session, serialized FIFO ops -----------
    enum class SftpKind { Read, Write, Stat, Readdir };
    enum class SftpPhase { NeedOpen, Transfer, NeedClose };

    struct SftpOp
    {
        quint64    reqId = 0;
        SftpKind   kind = SftpKind::Read;
        SftpPhase  phase = SftpPhase::NeedOpen;
        QString    path;
        int        handleId = -1;        // file/dir handle once opened
        QByteArray buffer;               // read accumulator / write source
        qint64     writeOffset = 0;      // bytes of `buffer` written so far
        QList<RemoteDirEntry> entries;   // readdir accumulator
    };

    void serviceSftp();          // advance the head op as far as possible now
    // Open the single SFTP session once (D1). Returns Ok when usable (latched),
    // Again while still establishing, Error when it cannot be opened.
    ISshTransport::Step ensureSftpInited();
    bool advanceSftpOp(SftpOp &op); // true = finished (signal emitted), false = wait
    void enqueueSftpOp(SftpOp op); // append + kick the engine
    void failSftpOp(const SftpOp &op, const QString &reason); // emit failure signal
    void failAllSftp(const QString &reason); // fail active + queued on loss

    // --- exec engine (D6): independent non-PTY channels, run concurrently -----
    // Unlike SFTP (one serialized session), each exec op owns its own channel
    // and they advance concurrently — git fetchers must not block each other.
    // The ops live OUTSIDE m_channels / m_rotation (they are not interactive PTY
    // channels): serviceExec drives them directly off their transportId so the
    // round-robin read pump never touches them.
    enum class ExecPhase
    {
        NeedOpen,   // openChannel in flight
        NeedExec,   // channel open; channel_exec(command) pending
        Streaming,  // exec started; draining stdout/stderr to EOF
        Done,       // terminal; pending removal
    };
    struct ExecOp
    {
        quint64    reqId = 0;
        int        transportId = -1;
        ExecPhase  phase = ExecPhase::NeedOpen;
        QString    command;
        QByteArray stdinPayload;   // fed once at Streaming start
        qint64     stdinOffset = 0;
        bool       stdinSent = false;
        // Streamed stdin (D8): bytes appended after start via requestExecWrite
        // (the ACP agent session keeps writing frames). Drained front-to-back on
        // each Streaming pass; never EOF'd while the op lives.
        QByteArray streamStdin;
        qint64     streamStdinOffset = 0;
        bool       stdoutEof = false;
        bool       stderrEof = false;
    };
    void serviceExec();             // advance every in-flight exec op one pass
    bool advanceExecOp(ExecOp &op); // true = finished (execDone emitted)
    void failAllExec(int exitStatus); // resolve every in-flight exec on loss

    std::unique_ptr<ISshTransport> m_transport;
    int m_channelCap;
    State m_state = State::Idle;

    ConnectParams m_params;
    bool m_hostKeyEmitted = false;
    bool m_hostKeyAccepted = false;
    bool m_hostKeyRejected = false;

    // Live + queued channels. m_channels holds every non-removed channel keyed
    // by logicalId; m_openQueue is the FIFO of Queued logicalIds (D5).
    QHash<int, Channel> m_channels;
    QList<int> m_openQueue;
    // Stable rotation order for the round-robin read sweep (insertion order of
    // Open channels). Kept as a list so every sweep visits each exactly once.
    QList<int> m_rotation;

    QSocketNotifier *m_readNotifier = nullptr;
    QSocketNotifier *m_writeNotifier = nullptr;
    bool m_writeNotifierWanted = false;
    bool m_lost = false;

    // SFTP engine state. m_sftpInited latches the one-and-only sftpInit so every
    // op reuses the single session (D1 one-channel-reuse invariant). The op
    // queue is serviced strictly FIFO, one op at a time (the SFTP session is
    // single-threaded like every other channel).
    bool m_sftpInited = false;
    QList<SftpOp> m_sftpQueue;

    // Exec engine state (D6). In-flight exec ops, each on its own channel,
    // advanced concurrently by serviceExec. Not keyed against the channel cap
    // (they are short-lived one-shots, not interactive PTYs).
    QList<ExecOp> m_execOps;
};

} // namespace remote

Q_DECLARE_METATYPE(remote::SshSessionWorker::ConnectParams)
Q_DECLARE_METATYPE(remote::RemoteDirEntry)
Q_DECLARE_METATYPE(QList<remote::RemoteDirEntry>)

#endif // REMOTE_SSHSESSIONWORKER_H
