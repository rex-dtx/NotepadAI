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

#ifndef REMOTE_LIBSSH2TRANSPORT_H
#define REMOTE_LIBSSH2TRANSPORT_H

#include <QByteArray>
#include <QHash>
#include <QString>

#include "ISshTransport.h"

// Forward-declare the libssh2 opaque types so this header does not require the
// vendored libssh2 sources to be present just to parse it. The .cpp includes
// <libssh2.h> for the real definitions.
struct _LIBSSH2_SESSION;
struct _LIBSSH2_CHANNEL;
struct _LIBSSH2_AGENT;
struct _LIBSSH2_SFTP;
struct _LIBSSH2_SFTP_HANDLE;

namespace remote {

// The thin production ISshTransport over libssh2 (1-2 libssh2 calls per method,
// no algorithm — all the state-machine logic lives in SshSessionWorker). The
// session is operated NON-BLOCKING; every step returns Again on
// LIBSSH2_ERROR_EAGAIN and is retried on the next socket-activity edge.
//
// Lives entirely on the worker thread (the worker is the only caller). Uses a
// raw TCP socket fd that the worker drives via QSocketNotifier.
class Libssh2Transport : public ISshTransport
{
public:
    Libssh2Transport();
    ~Libssh2Transport() override;

    Step connectSocket(const QString &host, int port) override;
    Step handshake() override;
    QByteArray hostKey() const override;
    Step authPassword(const QString &username, const QString &password) override;
    Step authPublicKey(const QString &username, const QString &keyPath,
                       const QString &passphrase) override;
    Step authAgent(const QString &username) override;

    OpenResult openChannel() override;
    Step requestPty(int channelId, const QByteArray &term, int cols, int rows) override;
    Step resizePty(int channelId, int cols, int rows) override;
    Step execOrShell(int channelId, const QString &command) override;
    qint64 chWrite(int channelId, const QByteArray &bytes) override;
    ReadResult chRead(int channelId) override;
    ReadResult chReadStderr(int channelId) override;
    int chExitStatus(int channelId) override;
    void closeChannel(int channelId) override;

    SftpOpenResult sftpInit() override;
    void sftpShutdown() override;
    SftpOpenResult sftpOpen(const QString &path, bool forWrite) override;
    ReadResult sftpRead(int handleId) override;
    qint64 sftpWrite(int handleId, const QByteArray &bytes) override;
    void sftpClose(int handleId) override;
    SftpOpenResult sftpOpendir(const QString &path) override;
    SftpDirEntry sftpReaddir(int handleId) override;
    void sftpClosedir(int handleId) override;
    SftpStatResult sftpStat(const QString &path) override;

    qintptr socketFd() const override { return m_sock; }
    void disconnect() override;

private:
    _LIBSSH2_CHANNEL *channel(int channelId) const { return m_channels.value(channelId, nullptr); }
    _LIBSSH2_SFTP_HANDLE *sftpHandle(int handleId) const { return m_sftpHandles.value(handleId, nullptr); }

    _LIBSSH2_SESSION *m_session = nullptr;
    _LIBSSH2_AGENT *m_agent = nullptr;
    qintptr m_sock = -1;
    QByteArray m_hostKey;
    QHash<int, _LIBSSH2_CHANNEL *> m_channels;
    int m_nextChannelId = 1;
    void *m_agentPrevId = nullptr; // last-tried agent identity (auth walk)
    bool m_libssh2Inited = false;

    // SFTP state — the single reused session (D1) plus its open file/dir
    // handles keyed by the int ids this transport hands out.
    _LIBSSH2_SFTP *m_sftp = nullptr;
    QHash<int, _LIBSSH2_SFTP_HANDLE *> m_sftpHandles;
    int m_nextSftpHandleId = 1;
};

} // namespace remote

#endif // REMOTE_LIBSSH2TRANSPORT_H
