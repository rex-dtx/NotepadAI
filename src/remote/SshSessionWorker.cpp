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

#include "SshSessionWorker.h"

#include "SshHostKeyStore.h"

#include <QMetaType>
#include <QSocketNotifier>

#include <utility>

namespace remote {

SshSessionWorker::SshSessionWorker(std::unique_ptr<ISshTransport> transport,
                                   int channelCap, QObject *parent)
    : QObject(parent)
    , m_transport(std::move(transport))
    , m_channelCap(channelCap > 0 ? channelCap : 10)
{
    qRegisterMetaType<remote::SshSessionWorker::State>("remote::SshSessionWorker::State");
    qRegisterMetaType<remote::SshSessionWorker::ConnectParams>(
        "remote::SshSessionWorker::ConnectParams");
    qRegisterMetaType<remote::RemoteDirEntry>("remote::RemoteDirEntry");
    qRegisterMetaType<QList<remote::RemoteDirEntry>>("QList<remote::RemoteDirEntry>");
}

SshSessionWorker::~SshSessionWorker()
{
    teardownNotifiers();
    if (m_transport) {
        m_transport->disconnect();
    }
}

void SshSessionWorker::setState(State s)
{
    if (m_state == s) {
        return;
    }
    m_state = s;
    emit stateChanged(s);
}

int SshSessionWorker::liveChannelCount() const
{
    int n = 0;
    for (auto it = m_channels.constBegin(); it != m_channels.constEnd(); ++it) {
        const ChPhase p = it.value().phase;
        if (p != ChPhase::Queued && p != ChPhase::Closed) {
            ++n;
        }
    }
    return n;
}

SshSessionWorker::Channel *SshSessionWorker::channelByTransportId(int transportId)
{
    for (auto it = m_channels.begin(); it != m_channels.end(); ++it) {
        if (it.value().transportId == transportId) {
            return &it.value();
        }
    }
    return nullptr;
}

// --- connection entry points -------------------------------------------------

void SshSessionWorker::startConnect(const ConnectParams &params)
{
    m_params = params;
    m_lost = false;
    m_hostKeyEmitted = false;
    m_hostKeyAccepted = false;
    m_hostKeyRejected = false;
    // Fresh session → the SFTP layer is re-established lazily on the first op.
    m_sftpInited = false;
    setState(State::ConnectingSocket);
    pump();
}

void SshSessionWorker::acceptHostKey()
{
    m_hostKeyAccepted = true;
    pump();
}

void SshSessionWorker::rejectHostKey()
{
    m_hostKeyRejected = true;
    // The user refused the fingerprint — abort without authenticating.
    enterConnectionLost(tr("Host key rejected"));
}

void SshSessionWorker::requestDisconnect()
{
    if (m_state == State::Disconnected || m_state == State::Failed) {
        teardownNotifiers();
        if (m_transport) m_transport->disconnect();
        failAllSftp(tr("Disconnected"));
        failAllExec(-1);
        m_sftpInited = false;
        return;
    }
    teardownNotifiers();
    if (m_transport) {
        m_transport->disconnect();
    }
    // Resolve any pending SFTP callbacks before tearing the session down, so a
    // RemoteFsBackend op never hangs on a deliberate disconnect (D1).
    failAllSftp(tr("Disconnected"));
    failAllExec(-1);
    m_sftpInited = false;
    setState(State::Disconnected);
}

// --- channel request slots ---------------------------------------------------

void SshSessionWorker::requestOpenChannel(int logicalId, bool wantPty,
                                          const QByteArray &term, int cols, int rows,
                                          const QString &command)
{
    if (m_lost || m_state == State::Disconnected || m_state == State::Failed) {
        emit channelOpenFailed(logicalId, tr("Not connected"));
        return;
    }
    Channel ch;
    ch.logicalId = logicalId;
    ch.phase = ChPhase::Queued;   // promoted to Opening by tryStartQueued under the cap
    ch.wantPty = wantPty;
    ch.term = term.isEmpty() ? QByteArrayLiteral("xterm-256color") : term;
    ch.cols = cols > 0 ? cols : 80;
    ch.rows = rows > 0 ? rows : 24;
    ch.command = command;
    m_channels.insert(logicalId, ch);
    m_openQueue.append(logicalId);   // FIFO (D5): never dropped
    pump();
}

void SshSessionWorker::requestResize(int logicalId, int cols, int rows)
{
    auto it = m_channels.find(logicalId);
    if (it == m_channels.end()) {
        return;
    }
    it->cols = cols > 0 ? cols : it->cols;
    it->rows = rows > 0 ? rows : it->rows;
    if (it->phase == ChPhase::Open && !m_lost) {
        m_transport->resizePty(it->transportId, it->cols, it->rows);
    }
}

void SshSessionWorker::requestWrite(int logicalId, const QByteArray &bytes)
{
    auto it = m_channels.find(logicalId);
    if (it == m_channels.end() || it->phase == ChPhase::Closed || m_lost) {
        return; // dead-guard: nothing posted to a gone channel/session
    }
    it->pending.append(bytes);   // move-append, no per-byte allocation
    flushPendingWrites();
}

void SshSessionWorker::requestCloseChannel(int logicalId)
{
    auto it = m_channels.find(logicalId);
    if (it == m_channels.end()) {
        return;
    }
    // finishChannel frees the underlying transport channel (when live), so do
    // NOT close it here too — that would double-free.
    finishChannel(logicalId, -1);
}

// --- the pump: a single bounded sweep (D3) -----------------------------------

void SshSessionWorker::pump()
{
    if (m_lost) {
        return;
    }

    if (m_state == State::ConnectingSocket || m_state == State::Handshaking
        || m_state == State::AwaitingHostKey || m_state == State::Authenticating) {
        advanceConnect();
    }

    if (m_state != State::Ready) {
        return;
    }

    // Ready: progress any channels still being set up, drain reads round-robin,
    // then flush writes (which toggles the write notifier per D4).
    tryStartQueued();
    advanceChannelSetup();
    readSweep();
    flushPendingWrites();
    // The exec engine runs its own non-PTY channels off to the side; advance
    // them after the interactive pump so a large git output never starves the
    // terminal channels (and SFTP is serviced via onSocketActivity/serviceSftp).
    serviceExec();
}

void SshSessionWorker::advanceConnect()
{
    // Socket connect.
    if (m_state == State::ConnectingSocket) {
        const ISshTransport::Step s = m_transport->connectSocket(m_params.host, m_params.port);
        if (s == ISshTransport::Step::Again) return;
        if (s == ISshTransport::Step::Error) {
            enterConnectionLost(tr("Could not reach %1:%2").arg(m_params.host).arg(m_params.port));
            return;
        }
        setupNotifiers();
        setState(State::Handshaking);
    }

    // SSH handshake (key exchange).
    if (m_state == State::Handshaking) {
        const ISshTransport::Step s = m_transport->handshake();
        if (s == ISshTransport::Step::Again) return;
        if (s == ISshTransport::Step::Error) {
            enterConnectionLost(tr("SSH handshake failed"));
            return;
        }
        setState(State::AwaitingHostKey);
    }

    // Host-key verification gate. Emit the fingerprint once; auth is blocked
    // until acceptHostKey() (rejectHostKey() aborts via enterConnectionLost).
    if (m_state == State::AwaitingHostKey) {
        if (m_hostKeyRejected) {
            return; // already aborting
        }
        if (!m_hostKeyEmitted) {
            m_hostKeyEmitted = true;
            const QByteArray key = m_transport->hostKey();
            emit hostKeyReceived(SshHostKeyStore::sha256Fingerprint(key), key);
        }
        if (!m_hostKeyAccepted) {
            return; // wait for the UI / test to accept
        }
        setState(State::Authenticating);
    }

    // Authentication.
    if (m_state == State::Authenticating) {
        ISshTransport::Step s = ISshTransport::Step::Error;
        switch (m_params.authMethod) {
        case SshProfile::AuthMethod::Password: {
            const QString pw = m_params.passwordProvider ? m_params.passwordProvider() : QString();
            s = m_transport->authPassword(m_params.username, pw);
            break;
        }
        case SshProfile::AuthMethod::KeyFile: {
            const QString pass = m_params.passphraseProvider ? m_params.passphraseProvider() : QString();
            s = m_transport->authPublicKey(m_params.username, m_params.keyPath, pass);
            break;
        }
        case SshProfile::AuthMethod::Agent:
            s = m_transport->authAgent(m_params.username);
            break;
        }
        if (s == ISshTransport::Step::Again) return;
        if (s == ISshTransport::Step::Error) {
            const QString reason = tr("Authentication failed");
            emit authFailed(reason);
            enterConnectionLost(reason);
            return;
        }
        setState(State::Ready);
        emit connected();
    }
}

// --- channel-open FIFO queue (D5) --------------------------------------------

void SshSessionWorker::tryStartQueued()
{
    // Promote queued channels to Opening while under the cap. FIFO: always take
    // the head; never drop a request.
    while (!m_openQueue.isEmpty() && liveChannelCount() < m_channelCap) {
        const int logicalId = m_openQueue.takeFirst();
        auto it = m_channels.find(logicalId);
        if (it == m_channels.end()) {
            continue; // closed before it got a slot
        }
        it->phase = ChPhase::Opening; // now counts against the cap
    }
}

void SshSessionWorker::advanceChannelSetup()
{
    // Drive every not-yet-Open channel through open → (pty) → exec/shell.
    // Iterate over a snapshot of ids so finishChannel-driven removals are safe.
    const QList<int> ids = m_channels.keys();
    for (int logicalId : ids) {
        auto it = m_channels.find(logicalId);
        if (it == m_channels.end()) continue;
        Channel &ch = it.value();

        if (ch.phase == ChPhase::Opening) {
            const ISshTransport::OpenResult r = m_transport->openChannel();
            if (r.step == ISshTransport::Step::Again) {
                continue;
            }
            if (r.step == ISshTransport::Step::Error) {
                emit channelOpenFailed(logicalId, tr("Could not open channel"));
                finishChannel(logicalId, -1);
                continue;
            }
            ch.transportId = r.channelId;
            ch.phase = ch.wantPty ? ChPhase::NeedPty : ChPhase::NeedExec;
        }

        if (ch.phase == ChPhase::NeedPty) {
            const ISshTransport::Step s =
                m_transport->requestPty(ch.transportId, ch.term, ch.cols, ch.rows);
            if (s == ISshTransport::Step::Again) continue;
            if (s == ISshTransport::Step::Error) {
                emit channelOpenFailed(logicalId, tr("Could not allocate a PTY"));
                finishChannel(logicalId, -1);
                continue;
            }
            ch.phase = ChPhase::NeedExec;
        }

        if (ch.phase == ChPhase::NeedExec) {
            const ISshTransport::Step s = m_transport->execOrShell(ch.transportId, ch.command);
            if (s == ISshTransport::Step::Again) continue;
            if (s == ISshTransport::Step::Error) {
                emit channelOpenFailed(logicalId, tr("Could not start the remote shell"));
                finishChannel(logicalId, -1);
                continue;
            }
            ch.phase = ChPhase::Open;
            if (!m_rotation.contains(logicalId)) {
                m_rotation.append(logicalId);
            }
            emit channelOpened(logicalId);
        }
    }
}

// --- round-robin read sweep (D3) ---------------------------------------------

void SshSessionWorker::readSweep()
{
    // INVARIANT (anti-starvation): visit every Open channel exactly once per
    // sweep and drain it to EAGAIN. The outer loop advances regardless of how
    // much one channel produces, so a chatty channel cannot starve the others.
    // Time O(C + B); no per-byte allocation (chRead returns a QByteArray that we
    // MOVE into the dataReady payload); no scan of closed channels.
    const QList<int> order = m_rotation; // stable snapshot; safe across removals
    for (int logicalId : order) {
        auto it = m_channels.find(logicalId);
        if (it == m_channels.end() || it->phase != ChPhase::Open) {
            continue;
        }
        const int transportId = it->transportId;
        for (;;) {
            ISshTransport::ReadResult r = m_transport->chRead(transportId);
            if (r.error) {
                enterConnectionLost(tr("SSH connection lost"));
                return;
            }
            if (!r.data.isEmpty()) {
                emit dataReady(logicalId, std::move(r.data)); // queued to UI thread
            }
            if (r.eof) {
                finishChannel(logicalId, m_transport->chExitStatus(transportId));
                break;
            }
            if (r.again) {
                break; // channel drained; move to the next
            }
        }
    }
}

// --- write path + write-notifier invariant (D4) ------------------------------

void SshSessionWorker::flushPendingWrites()
{
    if (m_lost) {
        return;
    }
    bool anyPending = false;
    for (auto it = m_channels.begin(); it != m_channels.end(); ++it) {
        Channel &ch = it.value();
        if (ch.phase != ChPhase::Open || ch.pending.isEmpty()) {
            continue;
        }
        while (!ch.pending.isEmpty()) {
            const qint64 n = m_transport->chWrite(ch.transportId, ch.pending);
            if (n == ISshTransport::kWriteAgain) {
                anyPending = true;
                break; // send buffer full; wait for socket-writable
            }
            if (n == ISshTransport::kWriteError) {
                enterConnectionLost(tr("SSH connection lost"));
                return;
            }
            if (n <= 0) {
                break; // nothing consumed; avoid a spin
            }
            ch.pending.remove(0, static_cast<int>(n));
        }
    }
    // INVARIANT: the write notifier is enabled ONLY while bytes are pending and
    // is disabled the instant they drain — otherwise it fires continuously
    // (socket is almost always writable) and burns CPU.
    setWriteNotifierEnabled(anyPending);
}

// --- connection-loss cleanup (D8) --------------------------------------------

void SshSessionWorker::enterConnectionLost(const QString &reason)
{
    if (m_lost) {
        return;
    }
    m_lost = true; // mark dead FIRST — no further libssh2 calls on a dead session

    teardownNotifiers();

    // Close each channel reporting an abnormal exit (-1, never a fabricated
    // success). Already-read bytes were emitted synchronously during the sweep,
    // so no tail output is lost. Iterate a snapshot of ids.
    const QList<int> ids = m_channels.keys();
    for (int logicalId : ids) {
        auto it = m_channels.find(logicalId);
        if (it == m_channels.end()) continue;
        it->phase = ChPhase::Closed;
        emit channelClosed(logicalId, -1);
    }
    m_channels.clear();
    m_rotation.clear();
    m_openQueue.clear();

    // Fail every in-flight / queued SFTP op with the loss reason so RemoteFsBackend
    // resolves its pending callbacks instead of hanging (D1). No libssh2 calls —
    // the session is already dead; the handles died with it.
    failAllSftp(reason);
    // Resolve every in-flight exec op with an abnormal exit (-1) so a remote git
    // runner's callback fires instead of hanging (D6). The channels died with
    // the session — no libssh2 calls.
    failAllExec(-1);
    m_sftpInited = false;

    if (m_transport) {
        m_transport->disconnect();
    }
    setState(State::Disconnected);
    emit disconnected(reason);
}

void SshSessionWorker::finishChannel(int logicalId, int exitStatus)
{
    auto it = m_channels.find(logicalId);
    if (it == m_channels.end()) {
        return;
    }
    // Free the underlying transport channel if it was opened and the session is
    // still alive. (On connection loss the session is dead — enterConnectionLost
    // handles teardown without calling libssh2.)
    if (it->transportId >= 0 && !m_lost) {
        m_transport->closeChannel(it->transportId);
    }
    m_channels.erase(it);
    m_rotation.removeAll(logicalId);
    m_openQueue.removeAll(logicalId);
    emit channelClosed(logicalId, exitStatus);

    // A freed slot may let a queued open proceed (D5). Only meaningful while
    // still connected.
    if (!m_lost && m_state == State::Ready) {
        tryStartQueued();
        advanceChannelSetup();
    }
}

// --- notifiers (production only; tests drive pumpForTest) --------------------

void SshSessionWorker::setupNotifiers()
{
    const qintptr fd = m_transport->socketFd();
    if (fd < 0) {
        return; // fd not valid yet (or a fake transport in tests)
    }
    if (!m_readNotifier) {
        m_readNotifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(m_readNotifier, &QSocketNotifier::activated, this,
                &SshSessionWorker::onSocketActivity);
    }
    if (!m_writeNotifier) {
        m_writeNotifier = new QSocketNotifier(fd, QSocketNotifier::Write, this);
        m_writeNotifier->setEnabled(false); // D4: off until bytes pending
        connect(m_writeNotifier, &QSocketNotifier::activated, this,
                &SshSessionWorker::onSocketActivity);
    }
}

void SshSessionWorker::teardownNotifiers()
{
    if (m_readNotifier) {
        m_readNotifier->setEnabled(false);
        m_readNotifier->deleteLater();
        m_readNotifier = nullptr;
    }
    if (m_writeNotifier) {
        m_writeNotifier->setEnabled(false);
        m_writeNotifier->deleteLater();
        m_writeNotifier = nullptr;
    }
    m_writeNotifierWanted = false;
}

void SshSessionWorker::setWriteNotifierEnabled(bool on)
{
    m_writeNotifierWanted = on; // observable in tests
    if (m_writeNotifier) {
        m_writeNotifier->setEnabled(on);
    }
}

void SshSessionWorker::onSocketActivity()
{
    // A single readable/writable edge → one bounded pump sweep. Not recursive.
    pump();
    // The SFTP layer is multiplexed on the SAME socket, so the same edge may
    // carry SFTP progress (EAGAIN re-arm path, D1). Drive it after the channel
    // pump so interactive channel I/O is never starved by a large transfer.
    serviceSftp();
    // Exec channels share the same socket too; advance them on every edge so a
    // streaming git command resumes after EAGAIN (D6).
    serviceExec();
}

// --- SFTP engine (D1) --------------------------------------------------------
// One reused SFTP session (sftpInit once), serialized FIFO ops. Each request
// slot appends an op and kicks the engine; serviceSftp() advances the head op
// as far as it can this pass and re-arms on the next socket edge for EAGAIN.

void SshSessionWorker::requestSftpRead(quint64 reqId, const QString &path)
{
    SftpOp op;
    op.reqId = reqId;
    op.kind = SftpKind::Read;
    op.path = path;
    enqueueSftpOp(std::move(op));
}

void SshSessionWorker::requestSftpWrite(quint64 reqId, const QString &path,
                                        const QByteArray &data)
{
    SftpOp op;
    op.reqId = reqId;
    op.kind = SftpKind::Write;
    op.path = path;
    op.buffer = data; // write source; consumed front-to-back via writeOffset
    enqueueSftpOp(std::move(op));
}

void SshSessionWorker::requestSftpStat(quint64 reqId, const QString &path)
{
    SftpOp op;
    op.reqId = reqId;
    op.kind = SftpKind::Stat;
    op.path = path;
    enqueueSftpOp(std::move(op));
}

void SshSessionWorker::requestSftpReaddir(quint64 reqId, const QString &path)
{
    SftpOp op;
    op.reqId = reqId;
    op.kind = SftpKind::Readdir;
    op.path = path;
    enqueueSftpOp(std::move(op));
}

void SshSessionWorker::enqueueSftpOp(SftpOp op)
{
    // Dead-guard: never queue against a lost/closed session — fail fast so the
    // backend's callback resolves immediately rather than waiting forever.
    if (m_lost || m_state == State::Disconnected || m_state == State::Failed) {
        failSftpOp(op, tr("Not connected"));
        return;
    }
    m_sftpQueue.append(std::move(op));
    serviceSftp();
}

ISshTransport::Step SshSessionWorker::ensureSftpInited()
{
    if (m_sftpInited) {
        return ISshTransport::Step::Ok;
    }
    const ISshTransport::SftpOpenResult r = m_transport->sftpInit();
    if (r.step == ISshTransport::Step::Ok) {
        m_sftpInited = true;
    }
    return r.step;
}

void SshSessionWorker::serviceSftp()
{
    // Only run while the session is usable; otherwise leave ops queued (they
    // service once Ready) or, if truly dead, they were already failed in
    // enterConnectionLost / enqueueSftpOp.
    if (m_lost || m_state != State::Ready) {
        return;
    }
    // Advance the head op to completion-or-EAGAIN. A finished op is popped and
    // the next one is attempted in the same pass (cheap ops like an already
    // -buffered readdir/stat chain without an extra socket edge).
    while (!m_sftpQueue.isEmpty()) {
        const ISshTransport::Step initStep = ensureSftpInited();
        if (initStep == ISshTransport::Step::Again) {
            return; // session still establishing → retry on the next edge
        }
        if (initStep == ISshTransport::Step::Error) {
            // The session can never open → surface it as the head op's failure
            // so the caller is not stuck waiting forever.
            const SftpOp failed = m_sftpQueue.takeFirst();
            failSftpOp(failed, tr("Could not open SFTP session"));
            continue;
        }
        SftpOp &op = m_sftpQueue.first();
        if (!advanceSftpOp(op)) {
            return; // EAGAIN mid-op: resume on the next socket edge
        }
        m_sftpQueue.removeFirst(); // finished (a *Done signal was emitted)
    }
}

bool SshSessionWorker::advanceSftpOp(SftpOp &op)
{
    switch (op.kind) {
    case SftpKind::Stat: {
        const ISshTransport::SftpStatResult r = m_transport->sftpStat(op.path);
        if (r.step == ISshTransport::Step::Again) {
            return false;
        }
        if (r.step == ISshTransport::Step::Error) {
            // Not an I/O failure: a missing path is reported exists=false / ok=true
            // (mirrors the local QFileInfo backend, where absence is not an error).
            emit sftpStatDone(op.reqId, /*ok=*/true, /*exists=*/false, /*isDir=*/false,
                              /*size=*/0, /*mtime=*/0, QString());
            return true;
        }
        const ISshTransport::SftpAttrs &a = r.attrs;
        emit sftpStatDone(op.reqId, /*ok=*/true, /*exists=*/true, a.isDir,
                          static_cast<qint64>(a.hasSize ? a.size : 0),
                          static_cast<qint64>(a.hasMtime ? a.mtime : 0), QString());
        return true;
    }

    case SftpKind::Read: {
        if (op.phase == SftpPhase::NeedOpen) {
            const ISshTransport::SftpOpenResult r = m_transport->sftpOpen(op.path, /*forWrite=*/false);
            if (r.step == ISshTransport::Step::Again) {
                return false;
            }
            if (r.step == ISshTransport::Step::Error) {
                failSftpOp(op, tr("Could not open remote file for reading"));
                return true;
            }
            op.handleId = r.handleId;
            op.phase = SftpPhase::Transfer;
        }
        // Drain chunks into the reused buffer until EOF / EAGAIN.
        for (;;) {
            ISshTransport::ReadResult rr = m_transport->sftpRead(op.handleId);
            if (rr.error) {
                m_transport->sftpClose(op.handleId);
                op.handleId = -1;
                failSftpOp(op, tr("Remote read failed"));
                return true;
            }
            if (!rr.data.isEmpty()) {
                op.buffer.append(rr.data); // move-append; chunked accumulation
            }
            if (rr.eof) {
                m_transport->sftpClose(op.handleId);
                op.handleId = -1;
                emit sftpReadDone(op.reqId, /*ok=*/true, std::move(op.buffer), QString());
                return true;
            }
            if (rr.again) {
                return false; // resume on the next socket edge
            }
        }
    }

    case SftpKind::Write: {
        if (op.phase == SftpPhase::NeedOpen) {
            const ISshTransport::SftpOpenResult r = m_transport->sftpOpen(op.path, /*forWrite=*/true);
            if (r.step == ISshTransport::Step::Again) {
                return false;
            }
            if (r.step == ISshTransport::Step::Error) {
                failSftpOp(op, tr("Could not open remote file for writing"));
                return true;
            }
            op.handleId = r.handleId;
            op.phase = SftpPhase::Transfer;
        }
        while (op.writeOffset < op.buffer.size()) {
            // Zero-copy view of the not-yet-written tail (no per-iteration alloc).
            const QByteArray slice = QByteArray::fromRawData(
                op.buffer.constData() + op.writeOffset,
                static_cast<int>(op.buffer.size() - op.writeOffset));
            const qint64 n = m_transport->sftpWrite(op.handleId, slice);
            if (n == ISshTransport::kWriteAgain) {
                setWriteNotifierEnabled(true); // socket-writable edge resumes us
                return false;
            }
            if (n == ISshTransport::kWriteError) {
                m_transport->sftpClose(op.handleId);
                op.handleId = -1;
                failSftpOp(op, tr("Remote write failed"));
                return true;
            }
            if (n <= 0) {
                return false; // nothing consumed; avoid a spin, retry next edge
            }
            op.writeOffset += n;
        }
        m_transport->sftpClose(op.handleId);
        op.handleId = -1;
        emit sftpWriteDone(op.reqId, /*ok=*/true, QString());
        return true;
    }

    case SftpKind::Readdir: {
        if (op.phase == SftpPhase::NeedOpen) {
            const ISshTransport::SftpOpenResult r = m_transport->sftpOpendir(op.path);
            if (r.step == ISshTransport::Step::Again) {
                return false;
            }
            if (r.step == ISshTransport::Step::Error) {
                failSftpOp(op, tr("Could not open remote directory"));
                return true;
            }
            op.handleId = r.handleId;
            op.phase = SftpPhase::Transfer;
        }
        for (;;) {
            ISshTransport::SftpDirEntry e = m_transport->sftpReaddir(op.handleId);
            if (e.kind == ISshTransport::SftpDirEntry::Kind::Again) {
                return false; // resume on the next socket edge
            }
            if (e.kind == ISshTransport::SftpDirEntry::Kind::Error) {
                m_transport->sftpClosedir(op.handleId);
                op.handleId = -1;
                failSftpOp(op, tr("Remote directory listing failed"));
                return true;
            }
            if (e.kind == ISshTransport::SftpDirEntry::Kind::Done) {
                m_transport->sftpClosedir(op.handleId);
                op.handleId = -1;
                emit sftpReaddirDone(op.reqId, /*ok=*/true, op.entries, QString());
                return true;
            }
            // Kind::Entry — decode + filter "." / ".." (matches local readdir).
            const QString name = QString::fromUtf8(e.name);
            if (name == QLatin1String(".") || name == QLatin1String("..")) {
                continue;
            }
            RemoteDirEntry de;
            de.name = name;
            de.isDir = e.attrs.isDir;
            de.size = static_cast<qint64>(e.attrs.hasSize ? e.attrs.size : 0);
            de.mtimeSecs = static_cast<qint64>(e.attrs.hasMtime ? e.attrs.mtime : 0);
            op.entries.append(std::move(de));
        }
    }
    }
    return true; // unreachable; keeps the compiler happy on all paths
}

void SshSessionWorker::failSftpOp(const SftpOp &op, const QString &reason)
{
    switch (op.kind) {
    case SftpKind::Read:
        emit sftpReadDone(op.reqId, /*ok=*/false, QByteArray(), reason);
        break;
    case SftpKind::Write:
        emit sftpWriteDone(op.reqId, /*ok=*/false, reason);
        break;
    case SftpKind::Stat:
        emit sftpStatDone(op.reqId, /*ok=*/false, /*exists=*/false, /*isDir=*/false,
                          0, 0, reason);
        break;
    case SftpKind::Readdir:
        emit sftpReaddirDone(op.reqId, /*ok=*/false, QList<RemoteDirEntry>(), reason);
        break;
    }
}

void SshSessionWorker::failAllSftp(const QString &reason)
{
    // Snapshot + clear first so no re-entrant enqueue can resurrect the queue.
    const QList<SftpOp> pending = std::move(m_sftpQueue);
    m_sftpQueue.clear();
    for (const SftpOp &op : pending) {
        failSftpOp(op, reason);
    }
}

// --- exec engine (D6) --------------------------------------------------------
// Each exec op owns a dedicated non-PTY channel and advances concurrently with
// the others. A request appends an op and kicks the engine; serviceExec() walks
// every op once per pass, opening / exec'ing / draining stdout+stderr until the
// streams hit EOF, then emits execDone with the channel exit status.

void SshSessionWorker::requestExec(quint64 reqId, const QString &command,
                                   const QByteArray &stdinPayload)
{
    // Dead-guard: never run against a lost/closed session — resolve immediately
    // so the runner's callback fires instead of hanging.
    if (m_lost || m_state == State::Disconnected || m_state == State::Failed) {
        emit execDone(reqId, -1);
        return;
    }
    ExecOp op;
    op.reqId = reqId;
    op.command = command;
    op.stdinPayload = stdinPayload;
    m_execOps.append(std::move(op));
    serviceExec();
}

void SshSessionWorker::requestExecCancel(quint64 reqId)
{
    for (int i = 0; i < m_execOps.size(); ++i) {
        if (m_execOps[i].reqId != reqId) {
            continue;
        }
        // Free the channel if it was opened and the session is still alive. The
        // runner already resolved its callback, so we emit NO execDone here.
        if (m_execOps[i].transportId >= 0 && !m_lost) {
            m_transport->closeChannel(m_execOps[i].transportId);
        }
        m_execOps.removeAt(i);
        return;
    }
}

void SshSessionWorker::requestExecWrite(quint64 reqId, const QByteArray &bytes)
{
    // Append to the matching in-flight op's streamed-stdin buffer (D8). If the op
    // is gone (already finished/cancelled) the write is silently dropped — the
    // remote process is no longer there to read it. Bytes drain on the next
    // service pass; serviceExec() kicks one now so a frame written while Ready
    // goes out promptly (and EAGAIN re-arms the write notifier for the rest).
    for (int i = 0; i < m_execOps.size(); ++i) {
        if (m_execOps[i].reqId != reqId) {
            continue;
        }
        m_execOps[i].streamStdin.append(bytes);
        serviceExec();
        return;
    }
}

void SshSessionWorker::serviceExec()
{
    if (m_lost || m_state != State::Ready) {
        return; // ops stay queued; they advance once Ready / are failed on loss
    }
    // Advance each op one pass; remove finished ops. Index walk is safe because
    // advanceExecOp never appends, and a finished op is erased in place.
    for (int i = 0; i < m_execOps.size();) {
        if (advanceExecOp(m_execOps[i])) {
            m_execOps.removeAt(i);
        } else {
            ++i;
        }
    }
}

bool SshSessionWorker::advanceExecOp(ExecOp &op)
{
    if (op.phase == ExecPhase::NeedOpen) {
        const ISshTransport::OpenResult r = m_transport->openChannel();
        if (r.step == ISshTransport::Step::Again) {
            return false;
        }
        if (r.step == ISshTransport::Step::Error) {
            emit execDone(op.reqId, -1);
            return true;
        }
        op.transportId = r.channelId;
        op.phase = ExecPhase::NeedExec;
    }

    if (op.phase == ExecPhase::NeedExec) {
        // No PTY: a raw exec channel keeps stdout and stderr as distinct
        // streams (chRead vs chReadStderr), which the git runner relies on.
        const ISshTransport::Step s = m_transport->execOrShell(op.transportId, op.command);
        if (s == ISshTransport::Step::Again) {
            return false;
        }
        if (s == ISshTransport::Step::Error) {
            m_transport->closeChannel(op.transportId);
            op.transportId = -1;
            emit execDone(op.reqId, -1);
            return true;
        }
        op.phase = ExecPhase::Streaming;
    }

    // Streaming: feed stdin once, then drain stdout + stderr toward EOF.
    if (op.phase == ExecPhase::Streaming) {
        if (!op.stdinSent && !op.stdinPayload.isEmpty()) {
            while (op.stdinOffset < op.stdinPayload.size()) {
                const QByteArray slice = QByteArray::fromRawData(
                    op.stdinPayload.constData() + op.stdinOffset,
                    static_cast<int>(op.stdinPayload.size() - op.stdinOffset));
                const qint64 n = m_transport->chWrite(op.transportId, slice);
                if (n == ISshTransport::kWriteAgain) {
                    setWriteNotifierEnabled(true); // resume on socket-writable
                    break;
                }
                if (n == ISshTransport::kWriteError) {
                    m_transport->closeChannel(op.transportId);
                    op.transportId = -1;
                    emit execDone(op.reqId, -1);
                    return true;
                }
                if (n <= 0) {
                    break; // nothing consumed; retry next edge
                }
                op.stdinOffset += n;
            }
            if (op.stdinOffset >= op.stdinPayload.size()) {
                op.stdinSent = true;
            }
        } else {
            op.stdinSent = true;
        }

        // Drain streamed stdin (D8): frames appended after start via
        // requestExecWrite. Only after the one-shot payload is fully sent, so
        // byte order on the wire is payload-then-streamed. EAGAIN re-arms the
        // write notifier; the rest drains on the next writable edge. Consumed
        // bytes are compacted out of the buffer so it does not grow unbounded
        // across a long session.
        if (op.stdinSent && op.streamStdinOffset < op.streamStdin.size()) {
            while (op.streamStdinOffset < op.streamStdin.size()) {
                const QByteArray slice = QByteArray::fromRawData(
                    op.streamStdin.constData() + op.streamStdinOffset,
                    static_cast<int>(op.streamStdin.size() - op.streamStdinOffset));
                const qint64 n = m_transport->chWrite(op.transportId, slice);
                if (n == ISshTransport::kWriteAgain) {
                    setWriteNotifierEnabled(true); // resume on socket-writable
                    break;
                }
                if (n == ISshTransport::kWriteError) {
                    m_transport->closeChannel(op.transportId);
                    op.transportId = -1;
                    emit execDone(op.reqId, -1);
                    return true;
                }
                if (n <= 0) {
                    break; // nothing consumed; retry next edge
                }
                op.streamStdinOffset += n;
            }
            if (op.streamStdinOffset >= op.streamStdin.size()) {
                op.streamStdin.clear();
                op.streamStdinOffset = 0;
            }
        }

        // Drain stdout.
        if (!op.stdoutEof) {
            for (;;) {
                ISshTransport::ReadResult r = m_transport->chRead(op.transportId);
                if (r.error) {
                    m_transport->closeChannel(op.transportId);
                    op.transportId = -1;
                    emit execDone(op.reqId, -1);
                    return true;
                }
                if (!r.data.isEmpty()) {
                    emit execStdoutChunk(op.reqId, std::move(r.data));
                }
                if (r.eof) {
                    op.stdoutEof = true;
                    break;
                }
                if (r.again) {
                    break; // resume on next edge
                }
            }
        }

        // Drain stderr (separate extended-data stream).
        if (!op.stderrEof) {
            for (;;) {
                ISshTransport::ReadResult r = m_transport->chReadStderr(op.transportId);
                if (r.error) {
                    m_transport->closeChannel(op.transportId);
                    op.transportId = -1;
                    emit execDone(op.reqId, -1);
                    return true;
                }
                if (!r.data.isEmpty()) {
                    emit execStderrChunk(op.reqId, std::move(r.data));
                }
                if (r.eof) {
                    op.stderrEof = true;
                    break;
                }
                if (r.again) {
                    break;
                }
            }
        }

        // The remote process has fully finished once stdout reaches EOF (the
        // channel-level EOF the transport reports tracks both streams). Read the
        // exit status, close, and resolve. stderr that did not explicitly EOF is
        // drained one last time above on this same pass.
        if (op.stdoutEof) {
            const int exitStatus = m_transport->chExitStatus(op.transportId);
            m_transport->closeChannel(op.transportId);
            op.transportId = -1;
            emit execDone(op.reqId, exitStatus);
            return true;
        }
        return false; // still streaming; resume on the next socket edge
    }

    return false;
}

void SshSessionWorker::failAllExec(int exitStatus)
{
    // Snapshot + clear so a re-entrant requestExec can't resurrect the list.
    const QList<ExecOp> pending = std::move(m_execOps);
    m_execOps.clear();
    for (const ExecOp &op : pending) {
        emit execDone(op.reqId, exitStatus);
    }
}

} // namespace remote
