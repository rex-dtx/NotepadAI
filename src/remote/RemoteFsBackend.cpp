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

#include "RemoteFsBackend.h"

#include "SshConnection.h"

#include <utility>

namespace remote {

RemoteFsBackend::RemoteFsBackend(SshConnection *connection, QObject *parent)
    : IFileSystemBackend(parent)
    , m_connection(connection)
{
    if (m_connection) {
        // Result signals are relayed queued from the worker thread by
        // SshConnection; resolve them to the right pending callback by reqId.
        connect(m_connection, &SshConnection::sftpReadResult,
                this, &RemoteFsBackend::onReadResult);
        connect(m_connection, &SshConnection::sftpWriteResult,
                this, &RemoteFsBackend::onWriteResult);
        connect(m_connection, &SshConnection::sftpStatResult,
                this, &RemoteFsBackend::onStatResult);
        connect(m_connection, &SshConnection::sftpReaddirResult,
                this, &RemoteFsBackend::onReaddirResult);
    }
}

RemoteFsBackend::~RemoteFsBackend() = default;

// --- async API ---------------------------------------------------------------

void RemoteFsBackend::readFileAsync(const QString &path, ReadCallback cb)
{
    if (!m_connection) {
        if (cb) cb(false, QByteArray(), tr("No SSH connection"));
        return;
    }
    const quint64 reqId = ++m_nextReqId;
    if (cb) m_readCallbacks.insert(reqId, std::move(cb));
    m_connection->sftpRead(reqId, path);
}

void RemoteFsBackend::writeFileAsync(const QString &path, const QByteArray &data,
                                     WriteCallback cb)
{
    if (!m_connection) {
        if (cb) cb(false, tr("No SSH connection"));
        return;
    }
    const quint64 reqId = ++m_nextReqId;
    if (cb) m_writeCallbacks.insert(reqId, std::move(cb));
    m_connection->sftpWrite(reqId, path, data);
}

void RemoteFsBackend::statAsync(const QString &path, StatCallback cb)
{
    if (!m_connection) {
        if (cb) cb(false, FileStat(), tr("No SSH connection"));
        return;
    }
    const quint64 reqId = ++m_nextReqId;
    if (cb) m_statCallbacks.insert(reqId, std::move(cb));
    m_connection->sftpStat(reqId, path);
}

void RemoteFsBackend::readdirAsync(const QString &path, ReaddirCallback cb)
{
    if (!m_connection) {
        if (cb) cb(false, QList<RemoteDirEntry>(), tr("No SSH connection"));
        return;
    }
    const quint64 reqId = ++m_nextReqId;
    if (cb) m_readdirCallbacks.insert(reqId, std::move(cb));
    m_connection->sftpReaddir(reqId, path);
}

// --- result handlers (UI thread) ---------------------------------------------

void RemoteFsBackend::onReadResult(quint64 reqId, bool ok, const QByteArray &data,
                                   const QString &error)
{
    auto it = m_readCallbacks.find(reqId);
    if (it == m_readCallbacks.end()) {
        return; // not ours (or already resolved)
    }
    const ReadCallback cb = std::move(it.value());
    m_readCallbacks.erase(it);
    if (cb) cb(ok, data, error);
}

void RemoteFsBackend::onWriteResult(quint64 reqId, bool ok, const QString &error)
{
    auto it = m_writeCallbacks.find(reqId);
    if (it == m_writeCallbacks.end()) {
        return;
    }
    const WriteCallback cb = std::move(it.value());
    m_writeCallbacks.erase(it);
    if (cb) cb(ok, error);
}

void RemoteFsBackend::onStatResult(quint64 reqId, bool ok, bool exists, bool isDir,
                                   qint64 size, qint64 mtimeSecs, const QString &error)
{
    auto it = m_statCallbacks.find(reqId);
    if (it == m_statCallbacks.end()) {
        return;
    }
    const StatCallback cb = std::move(it.value());
    m_statCallbacks.erase(it);
    FileStat s;
    s.exists = exists;
    s.isDir = isDir;
    s.size = size;
    if (mtimeSecs > 0) {
        s.lastModified = QDateTime::fromSecsSinceEpoch(mtimeSecs);
    }
    if (cb) cb(ok, s, error);
}

void RemoteFsBackend::onReaddirResult(quint64 reqId, bool ok,
                                      const QList<RemoteDirEntry> &entries,
                                      const QString &error)
{
    auto it = m_readdirCallbacks.find(reqId);
    if (it == m_readdirCallbacks.end()) {
        return;
    }
    const ReaddirCallback cb = std::move(it.value());
    m_readdirCallbacks.erase(it);
    if (cb) cb(ok, entries, error);
}

// --- synchronous overrides (fail-closed for remote) --------------------------
// A remote filesystem cannot be read synchronously without blocking the UI on
// the network, which the single-connection design forbids. Remote callers use
// the *Async variants; these exist only to satisfy the interface.

QByteArray RemoteFsBackend::readFile(const QString &path, bool *ok)
{
    Q_UNUSED(path);
    if (ok) *ok = false;
    return {};
}

bool RemoteFsBackend::writeFile(const QString &path, const QByteArray &data)
{
    Q_UNUSED(path);
    Q_UNUSED(data);
    return false;
}

FileStat RemoteFsBackend::stat(const QString &path)
{
    Q_UNUSED(path);
    return {}; // exists = false
}

QStringList RemoteFsBackend::readdir(const QString &path)
{
    Q_UNUSED(path);
    return {};
}

} // namespace remote
