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

#ifndef REMOTE_ISSHTRANSPORT_H
#define REMOTE_ISSHTRANSPORT_H

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace remote {

// The injectable seam that sits DIRECTLY on top of the libssh2 surface — one or
// two libssh2 calls per method, no algorithm. Libssh2Transport is the thin
// production impl; FakeSshTransport scripts handshake/auth/channel behavior so
// the SshSessionWorker state machine, channel multiplexing, write-pending
// toggling and connection-loss cleanup are all tested OFFLINE (no real sshd).
//
// All methods run on the worker thread only (the worker is the sole caller).
// libssh2 is operated non-blocking, so every step can return Again and is
// retried on the next socket-readable/writable edge.
class ISshTransport
{
public:
    // Per-step outcome for the connect/auth/channel-setup state machine.
    enum class Step
    {
        Ok,     // completed
        Again,  // EAGAIN — retry on next socket activity
        Error,  // fatal — connection cannot proceed
    };

    // openChannel outcome: Ok carries a transport channel id.
    struct OpenResult
    {
        Step step = Step::Again;
        int  channelId = -1;
    };

    // chRead outcome. `data` may be non-empty alongside eof. `error` is a fatal
    // socket condition (SOCKET_RECV/DISCONNECT) — distinct from `again`.
    struct ReadResult
    {
        QByteArray data;
        bool again = false;
        bool eof = false;
        bool error = false;
    };

    // chWrite sentinels (returned in place of a byte count).
    static constexpr qint64 kWriteAgain = -1; // EAGAIN: send buffer full
    static constexpr qint64 kWriteError = -2; // fatal socket error

    // --- SFTP (D1) -----------------------------------------------------------
    // The SFTP layer is itself a channel multiplexed on the live session. The
    // worker opens ONE SFTP session (sftpInit) and reuses it for every
    // file/dir op — never a session-per-request. File and directory handles are
    // identified by an opaque int the transport assigns (mirrors openChannel's
    // channelId), so this interface never leaks a libssh2 pointer to its
    // callers. Every method is non-blocking and may return Again.

    // Decoded subset of LIBSSH2_SFTP_ATTRIBUTES the file tree + editor need.
    // The has* flags are false when the SFTP attrs did not carry that field;
    // callers treat a missing size as 0 and a missing mode as "not a directory".
    struct SftpAttrs
    {
        bool   isDir = false;
        bool   hasSize = false;
        bool   hasMtime = false;
        quint64 size = 0;
        quint64 mtime = 0;       // seconds since epoch (SFTP server clock)
        quint32 permissions = 0; // POSIX mode bits (0 if not provided)
    };

    // sftpInit / sftpOpen / sftpOpendir outcome: Ok carries a transport handle
    // id (sftpInit's id is unused — the session is implicit/singleton).
    struct SftpOpenResult
    {
        Step step = Step::Again;
        int  handleId = -1;
    };

    // sftpReaddir outcome. One entry per call (matches libssh2_sftp_readdir_ex):
    // `kind` distinguishes a real entry from end-of-stream / retry / fatal.
    struct SftpDirEntry
    {
        enum class Kind
        {
            Entry, // `name` + `attrs` valid
            Done,  // directory fully enumerated
            Again, // EAGAIN — retry on next socket activity
            Error, // fatal SFTP/socket condition
        };
        Kind       kind = Kind::Again;
        QByteArray name;  // raw filename bytes (no path), as sent by the server
        SftpAttrs  attrs;
    };

    // sftpStat outcome. `attrs` is only meaningful when step == Ok.
    struct SftpStatResult
    {
        Step      step = Step::Again;
        SftpAttrs attrs;
    };

    virtual ~ISshTransport() = default;

    // --- connect / auth ------------------------------------------------------
    virtual Step connectSocket(const QString &host, int port) = 0;
    virtual Step handshake() = 0;
    // Raw host-key blob captured during handshake (for fingerprinting + the
    // known_hosts compare). Empty until handshake completes.
    virtual QByteArray hostKey() const = 0;
    virtual Step authPassword(const QString &username, const QString &password) = 0;
    virtual Step authPublicKey(const QString &username,
                               const QString &keyPath,
                               const QString &passphrase) = 0;
    virtual Step authAgent(const QString &username) = 0;

    // --- channels ------------------------------------------------------------
    virtual OpenResult openChannel() = 0;
    virtual Step requestPty(int channelId, const QByteArray &term, int cols, int rows) = 0;
    virtual Step resizePty(int channelId, int cols, int rows) = 0;
    // command empty → interactive shell; otherwise exec the command.
    virtual Step execOrShell(int channelId, const QString &command) = 0;
    virtual qint64 chWrite(int channelId, const QByteArray &bytes) = 0;
    virtual ReadResult chRead(int channelId) = 0;
    // Reads the channel's STDERR stream (SSH extended-data id 1), separate from
    // chRead's stdout. Needed by the remote git/exec path (D6/D8) which must
    // keep stderr distinct from stdout. Same EAGAIN/eof/error semantics as
    // chRead. Production reads via libssh2_channel_read_ex(STDERR); the local
    // exec path keeps stdout/stderr split exactly as QProcess does.
    virtual ReadResult chReadStderr(int channelId) = 0;
    virtual int chExitStatus(int channelId) = 0;
    virtual void closeChannel(int channelId) = 0;

    // --- SFTP ops (D1) -------------------------------------------------------
    // Open the single reused SFTP session over the live connection. handleId in
    // the result is unused (the session is a singleton owned by the transport);
    // a non-Ok step means it could not be established yet (Again) or at all.
    virtual SftpOpenResult sftpInit() = 0;
    // Tear the SFTP session down (frees any dangling file/dir handles first).
    // Idempotent; safe to call even if sftpInit never succeeded.
    virtual void sftpShutdown() = 0;

    // File ops. sftpOpen: forWrite=false → O_RDONLY; forWrite=true →
    // O_WRONLY|O_CREAT|O_TRUNC with mode 0644. Returns a file handle id on Ok.
    virtual SftpOpenResult sftpOpen(const QString &path, bool forWrite) = 0;
    virtual ReadResult sftpRead(int handleId) = 0;
    virtual qint64 sftpWrite(int handleId, const QByteArray &bytes) = 0;
    virtual void sftpClose(int handleId) = 0;

    // Directory ops. sftpReaddir returns one entry per call until Done.
    virtual SftpOpenResult sftpOpendir(const QString &path) = 0;
    virtual SftpDirEntry sftpReaddir(int handleId) = 0;
    virtual void sftpClosedir(int handleId) = 0;

    // Stat a path (follows symlinks — LIBSSH2_SFTP_STAT).
    virtual SftpStatResult sftpStat(const QString &path) = 0;

    // Underlying socket fd, for the QSocketNotifier pump. -1 until connected.
    virtual qintptr socketFd() const = 0;

    // Tear down session + socket. After this, no other method is called.
    virtual void disconnect() = 0;
};

} // namespace remote

#endif // REMOTE_ISSHTRANSPORT_H
