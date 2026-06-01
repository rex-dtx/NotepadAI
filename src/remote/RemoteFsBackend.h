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

#ifndef REMOTE_REMOTEFSBACKEND_H
#define REMOTE_REMOTEFSBACKEND_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>

#include "IFileSystemBackend.h"
#include "SshSessionWorker.h" // remote::RemoteDirEntry

namespace remote {

class SshConnection;

// SFTP-backed IFileSystemBackend (D1). All filesystem work happens on the
// SshConnection's worker thread over the ONE reused SFTP session the worker
// opens via sftpInit (never a session/channel per op). The public API is
// ASYNC: each *Async call mints a reqId, registers the caller's callback, and
// posts the op to the worker; the matching sftp*Result signal (relayed queued
// from the worker to the UI thread by SshConnection) resolves the callback.
//
// Threading: this object lives on the UI thread; it never touches libssh2. It
// only posts queued requests and receives queued results — no locks.
//
// The synchronous IFileSystemBackend methods (readFile/writeFile/stat/readdir)
// are pure-virtual on the interface and so must be overridden, but a remote
// filesystem cannot be read synchronously without blocking the UI thread on the
// network — which the single-connection design forbids. They therefore
// fail-closed (absent / false / empty). Remote callers MUST use the *Async
// variants; the local QFile-backed backend keeps the synchronous contract.
//
// `directoryChanged(path)` is inherited from the interface; the remote
// poll-watcher (a later batch) drives it. Nothing emits it here yet.
//
// Complexity: readFile/writeFile are O(bytes) chunked into a reused buffer with
// a move into the result; stat/readdir are O(entries), one round-trip each.
class RemoteFsBackend : public IFileSystemBackend
{
    Q_OBJECT

public:
    // Result-delivery callbacks (invoked once, on the UI thread). `ok` is the
    // I/O-success flag; `error` carries a human reason when !ok. For statAsync
    // an absent path is ok=true with stat.exists=false (not an error), matching
    // the local QFileInfo backend. ReadCallback/WriteCallback are inherited from
    // IFileSystemBackend (same signatures) so readFileAsync/writeFileAsync can
    // `override` the interface's async contract.
    using StatCallback = std::function<void(bool ok, const FileStat &stat, const QString &error)>;
    using ReaddirCallback =
        std::function<void(bool ok, const QList<RemoteDirEntry> &entries, const QString &error)>;

    explicit RemoteFsBackend(SshConnection *connection, QObject *parent = nullptr);
    ~RemoteFsBackend() override;

    // SFTP-backed → remote.
    bool isRemote() const override { return true; }

    // --- async SFTP API (the real remote contract) --------------------------
    // readFileAsync/writeFileAsync override IFileSystemBackend's async methods so
    // the editor's open/save path drives them polymorphically; statAsync/
    // readdirAsync are remote-only extensions used by the file tree.
    void readFileAsync(const QString &path, ReadCallback cb) override;
    void writeFileAsync(const QString &path, const QByteArray &data, WriteCallback cb) override;
    void statAsync(const QString &path, StatCallback cb);
    void readdirAsync(const QString &path, ReaddirCallback cb);

    // --- synchronous IFileSystemBackend overrides (fail-closed for remote) --
    QByteArray readFile(const QString &path, bool *ok = nullptr) override;
    bool writeFile(const QString &path, const QByteArray &data) override;
    FileStat stat(const QString &path) override;
    QStringList readdir(const QString &path) override;

private:
    void onReadResult(quint64 reqId, bool ok, const QByteArray &data, const QString &error);
    void onWriteResult(quint64 reqId, bool ok, const QString &error);
    void onStatResult(quint64 reqId, bool ok, bool exists, bool isDir,
                      qint64 size, qint64 mtimeSecs, const QString &error);
    void onReaddirResult(quint64 reqId, bool ok,
                         const QList<RemoteDirEntry> &entries, const QString &error);

    QPointer<SshConnection> m_connection; // not owned (registry owns it)
    quint64 m_nextReqId = 0;              // monotonic; unique across all op kinds

    // Pending callbacks keyed by reqId. A given reqId lives in exactly one map
    // (the op kind it was minted for); the matching result signal carries it.
    QHash<quint64, ReadCallback> m_readCallbacks;
    QHash<quint64, WriteCallback> m_writeCallbacks;
    QHash<quint64, StatCallback> m_statCallbacks;
    QHash<quint64, ReaddirCallback> m_readdirCallbacks;
};

} // namespace remote

#endif // REMOTE_REMOTEFSBACKEND_H
