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

#include "RemoteGitProcessRunner.h"

#include "SshConnection.h"

#include <QTimer>

#include <utility>

namespace remote {

RemoteGitProcessRunner::RemoteGitProcessRunner(SshConnection *connection, QObject *parent)
    : QObject(parent)
    , m_connection(connection)
{
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, &RemoteGitProcessRunner::onTimeout);

    if (m_connection) {
        // Result signals are relayed queued from the worker thread by
        // SshConnection; resolve them to our in-flight op by reqId.
        connect(m_connection, &SshConnection::execStdout,
                this, &RemoteGitProcessRunner::onExecStdout);
        connect(m_connection, &SshConnection::execStderr,
                this, &RemoteGitProcessRunner::onExecStderr);
        connect(m_connection, &SshConnection::execDone,
                this, &RemoteGitProcessRunner::onExecDone);
    }
}

RemoteGitProcessRunner::~RemoteGitProcessRunner() = default;

// shellQuote / buildRemoteCommand are defined inline in the header (pure, no SSH
// dependency) so unit tests can assert them without linking the transport stack.

// --- run ----------------------------------------------------------------------

void RemoteGitProcessRunner::run(const QString &cwd,
                                 const QStringList &argv,
                                 const QByteArray &stdinPayload,
                                 int timeoutMs,
                                 bool readErrAsProgress,
                                 Callback cb)
{
    if (m_running) {
        // Caller violation — refuse rather than queue (matches the local runner).
        if (cb) cb(-1, {}, QByteArrayLiteral("runner busy"));
        return;
    }
    if (!m_connection) {
        if (cb) cb(-1, {}, QByteArrayLiteral("no SSH connection"));
        return;
    }

    reset();
    m_cb = std::move(cb);
    m_progressMode = readErrAsProgress;
    m_running = true;

    const QString command = buildRemoteCommand(cwd, argv);
    // SshConnection mints a process-wide-unique reqId and posts the exec request
    // onto the worker; results arrive via execStdout/execStderr/execDone. Git is
    // short-lived (open, run, close) → tag ShortLived for the FIX-2 admission
    // budget so a git op is never starved behind long-lived ACP channels.
    m_reqId = m_connection->execStart(command, stdinPayload, ExecKind::ShortLived);

    if (timeoutMs > 0) {
        m_timeout->start(timeoutMs);
    }
}

void RemoteGitProcessRunner::cancel()
{
    if (!m_running) {
        return;
    }
    m_cancelled = true;
    if (m_connection && m_reqId != 0) {
        m_connection->execCancel(m_reqId);
    }
    // Synchronous cancel: deliver the cancellation sentinel now (the local
    // runner's cancel() also resolves the callback). A late execDone for this
    // reqId is ignored — reset() clears m_reqId.
    finish(GitProcessRunner::kExitCancelled, {},
           m_stderrBuf.isEmpty() ? QByteArrayLiteral("cancelled") : m_stderrBuf);
}

void RemoteGitProcessRunner::cancelAsync()
{
    if (!m_running) {
        return;
    }
    if (m_connection && m_reqId != 0) {
        m_connection->execCancel(m_reqId);
    }
    // Drop the callback without firing it (spam-friendly contract); a late
    // execDone is ignored once m_reqId is cleared.
    m_cb = nullptr;
    reset();
}

void RemoteGitProcessRunner::onTimeout()
{
    if (!m_running) {
        return;
    }
    m_cancelled = true;
    if (m_connection && m_reqId != 0) {
        m_connection->appendDebugLog(
            QStringLiteral("git-timeout: req=%1").arg(m_reqId));
        m_connection->execCancel(m_reqId);
    }
    finish(GitProcessRunner::kExitCancelled, {}, QByteArrayLiteral("operation timed out"));
}

// --- result handlers (UI thread) ---------------------------------------------

void RemoteGitProcessRunner::onExecStdout(quint64 reqId, const QByteArray &chunk)
{
    if (reqId != m_reqId || !m_running) {
        return; // not ours / already resolved
    }
    m_stdoutBuf.append(chunk);
    if (m_maxOutputBytes > 0 && m_stdoutBuf.size() > m_maxOutputBytes && !m_truncated) {
        // Cap exceeded — mark truncated and cancel the remote op; the partial
        // bytes already buffered are returned to the caller (matches local).
        m_truncated = true;
        if (m_connection && m_reqId != 0) {
            m_connection->execCancel(m_reqId);
        }
        finish(GitProcessRunner::kExitTruncated, m_stdoutBuf, m_stderrBuf);
    }
}

void RemoteGitProcessRunner::onExecStderr(quint64 reqId, const QByteArray &chunk)
{
    if (reqId != m_reqId || !m_running) {
        return;
    }
    m_stderrBuf.append(chunk);
    if (m_progressMode) {
        m_stderrLineBuf.append(chunk);
        // git uses \r for in-place progress and \n at line end; split on both
        // (identical to GitProcessRunner::onReadyReadStderr).
        for (;;) {
            int nl = -1;
            for (int i = 0; i < m_stderrLineBuf.size(); ++i) {
                if (m_stderrLineBuf[i] == '\r' || m_stderrLineBuf[i] == '\n') {
                    nl = i;
                    break;
                }
            }
            if (nl < 0) break;
            const QByteArray line = m_stderrLineBuf.left(nl);
            m_stderrLineBuf.remove(0, nl + 1);
            const QString text = QString::fromUtf8(line).trimmed();
            if (!text.isEmpty()) emit progressLine(text);
        }
    }
}

void RemoteGitProcessRunner::onExecDone(quint64 reqId, int exitStatus)
{
    if (reqId != m_reqId || !m_running) {
        return;
    }
    if (m_truncated) {
        finish(GitProcessRunner::kExitTruncated, m_stdoutBuf, m_stderrBuf);
        return;
    }
    if (m_cancelled) {
        finish(GitProcessRunner::kExitCancelled, m_stdoutBuf,
               m_stderrBuf.isEmpty() ? QByteArrayLiteral("cancelled") : m_stderrBuf);
        return;
    }
    finish(exitStatus, m_stdoutBuf, m_stderrBuf);
}

void RemoteGitProcessRunner::finish(int exit, const QByteArray &out, const QByteArray &err)
{
    Callback cb = std::move(m_cb);
    const QByteArray &outCopy = out;
    const QByteArray &errCopy = err;
    reset();
    if (cb) cb(exit, outCopy, errCopy);
}

void RemoteGitProcessRunner::reset()
{
    if (m_timeout) m_timeout->stop();
    m_reqId = 0;
    m_running = false;
    m_cancelled = false;
    m_truncated = false;
    m_progressMode = false;
    m_cb = nullptr;
    m_stdoutBuf.clear();
    m_stderrBuf.clear();
    m_stderrLineBuf.clear();
    // m_maxOutputBytes is a config (set by caller once) — don't clear here.
}

} // namespace remote
