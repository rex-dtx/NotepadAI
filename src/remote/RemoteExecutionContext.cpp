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

#include "RemoteExecutionContext.h"

#include "RemoteFsBackend.h"
#include "RemoteGitProcessRunner.h"
#include "SshConnection.h"
#include "SshPtyProcess.h"

#include <QDir>
#include <QPointer>
#include <QTimer>

#include <memory>

namespace remote {

namespace {

// Map the worker/connection state machine onto the ExecutionContext::State enum.
ExecutionContext::State mapState(SshConnection::State s)
{
    switch (s) {
    case SshSessionWorker::State::Idle:
    case SshSessionWorker::State::Disconnected:
        return ExecutionContext::State::Disconnected;
    case SshSessionWorker::State::ConnectingSocket:
    case SshSessionWorker::State::Handshaking:
    case SshSessionWorker::State::AwaitingHostKey:
    case SshSessionWorker::State::Authenticating:
        return ExecutionContext::State::Connecting;
    case SshSessionWorker::State::Ready:
        return ExecutionContext::State::Connected;
    case SshSessionWorker::State::Failed:
        return ExecutionContext::State::Failed;
    }
    return ExecutionContext::State::Disconnected;
}

// POSIX single-quote one token (close/escape/reopen for embedded quotes) — same
// rule as RemoteGitProcessRunner::shellQuote, kept local so exec() is
// self-contained. Safe for arbitrary bytes; no shell metachar survives.
QString shQuote(const QString &token)
{
    QString out;
    out.reserve(token.size() + 2);
    out.append(QLatin1Char('\''));
    for (const QChar c : token) {
        if (c == QLatin1Char('\'')) {
            out.append(QLatin1String("'\\''"));
        } else {
            out.append(c);
        }
    }
    out.append(QLatin1Char('\''));
    return out;
}

// `cd <cwd> && <argv0> <argv1...>` with every token shell-quoted; empty cwd skips
// the cd prefix (runs in the channel's default dir, e.g. the remote home).
QString buildExecCommand(const QString &cwd, const QStringList &argv)
{
    QString cmd;
    if (!cwd.isEmpty()) {
        cmd += QStringLiteral("cd ") + shQuote(cwd) + QStringLiteral(" && ");
    }
    for (int i = 0; i < argv.size(); ++i) {
        if (i > 0) {
            cmd += QLatin1Char(' ');
        }
        cmd += shQuote(argv.at(i));
    }
    return cmd;
}

} // namespace

RemoteExecutionContext::RemoteExecutionContext(const SshProfile &profile,
                                               SshConnection *connection, QObject *parent)
    : ExecutionContext(parent)
    , m_profile(profile)
    , m_connection(connection)
{
    if (m_connection) {
        m_state = mapState(m_connection->state());

        connect(m_connection, &SshConnection::stateChanged, this,
                [this](SshConnection::State s) {
                    const State mapped = mapState(s);
                    if (mapped != m_state) {
                        m_state = mapped;
                        emit stateChanged(m_state);
                    }
                });
        connect(m_connection, &SshConnection::connectionLost, this,
                [this](const QString &reason) {
                    if (m_state != State::Disconnected) {
                        m_state = State::Disconnected;
                        emit stateChanged(m_state);
                    }
                    emit connectionLost(reason);
                });
        connect(m_connection, &SshConnection::connected, this, [this]() {
            // reconnected() vs first connect: emit reconnected only if we had
            // previously been Connected then dropped. Phase 1 keeps this simple
            // — the auto-reconnect flow is Phase 2; we just surface the signal.
            emit reconnected();
        });
    }
}

RemoteExecutionContext::~RemoteExecutionContext() = default;

QString RemoteExecutionContext::displayName() const
{
    QString who = m_profile.username.isEmpty()
                      ? m_profile.host
                      : (m_profile.username + QLatin1Char('@') + m_profile.host);
    return who;
}

IPtyProcess *RemoteExecutionContext::createPty(QObject *parent)
{
    if (!m_connection) {
        return nullptr;
    }
    return new SshPtyProcess(m_connection, parent);
}

IGitProcessRunner *RemoteExecutionContext::createGitRunner(QObject *parent)
{
    // Git over SSH (D6): a RemoteGitProcessRunner over this context's single
    // multiplexed connection. Without a connection there is nothing to back it,
    // so fall back to nullptr (callers null-check / default to local).
    if (!m_connection) {
        return nullptr;
    }
    return new RemoteGitProcessRunner(m_connection, parent);
}

void RemoteExecutionContext::exec(const QString &cwd, const QStringList &argv,
                                  const QByteArray &stdinPayload, int timeoutMs,
                                  ExecCallback cb)
{
    // One-shot remote exec over an SSH exec channel (D8): `cd <cwd> && <argv...>`
    // shell-quoted, run on the host, stdout/stderr captured and the exit status
    // returned to `cb` exactly once. Used for the ACP binary probe (`command -v`)
    // and any other capture-style remote command. Backed by the same multiplexed
    // exec engine the git runner uses (execStart/execStdout/execStderr/execDone),
    // so it shares the single connection — no new TCP per call.
    if (!m_connection) {
        if (cb) cb(-1, {}, QByteArrayLiteral("no SSH connection"));
        return;
    }
    if (argv.isEmpty()) {
        if (cb) cb(-1, {}, QByteArrayLiteral("empty argv"));
        return;
    }

    // Resolve cwd through the remote path policy when one is given; an empty cwd
    // runs in the channel's default dir (e.g. the remote home) — used by the ACP
    // binary probe, which must not be rebased onto the profile's lastRemotePath.
    const QString effectiveCwd = cwd.isEmpty() ? QString() : resolveCwd(cwd);
    const QString effectiveCommand = buildExecCommand(effectiveCwd, argv);

    SshConnection *conn = m_connection;
    const quint64 reqId = conn->execStart(effectiveCommand, stdinPayload);

    // A small collector lives until the op resolves. It filters the connection's
    // reqId-keyed relay signals down to this op, accumulates stdout/stderr, and
    // fires `cb` exactly once on execDone or timeout. Held by shared_ptr so a late
    // signal (e.g. the timeout racing execDone, both queued) safely sees
    // resolved==true and no-ops instead of touching freed memory; `guard` (the
    // signal-connection context) is torn down via deleteLater on resolution.
    struct Collector
    {
        QByteArray out;
        QByteArray err;
        bool resolved = false;
    };
    auto state = std::make_shared<Collector>();
    auto *guard = new QObject(this); // parents the connections; deleted on finish
    QPointer<RemoteExecutionContext> selfGuard(this);

    auto finish = [state, guard, cb](int exitCode) {
        if (state->resolved) {
            return;
        }
        state->resolved = true;
        if (cb) {
            cb(exitCode, state->out, state->err);
        }
        guard->deleteLater();
    };

    connect(conn, &SshConnection::execStdout, guard,
            [state, reqId](quint64 id, const QByteArray &chunk) {
                if (id == reqId && !state->resolved) state->out.append(chunk);
            });
    connect(conn, &SshConnection::execStderr, guard,
            [state, reqId](quint64 id, const QByteArray &chunk) {
                if (id == reqId && !state->resolved) state->err.append(chunk);
            });
    connect(conn, &SshConnection::execDone, guard,
            [reqId, finish](quint64 id, int exitStatus) {
                if (id == reqId) finish(exitStatus);
            });

    if (timeoutMs > 0) {
        QTimer *timer = new QTimer(guard);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, guard, [selfGuard, conn, reqId, finish]() {
            if (selfGuard && conn) {
                conn->execCancel(reqId);
            }
            finish(-1);
        });
        timer->start(timeoutMs);
    }
}

IFileSystemBackend *RemoteExecutionContext::fsBackend()
{
    // SFTP-backed remote filesystem (D1), multiplexed on this context's single
    // SSH connection. Lazily created and reused; parented to this context so it
    // shares its lifetime. Without a connection there is nothing to back it.
    if (!m_connection) {
        return nullptr;
    }
    if (!m_fsBackend) {
        m_fsBackend = new RemoteFsBackend(m_connection, this);
    }
    return m_fsBackend;
}

QString RemoteExecutionContext::resolveCwd(const QString &requested) const
{
    // Remote path: POSIX-normalize WITHOUT any local QFileInfo check (the path
    // lives on another machine). See design D11.
    QString path = requested;
    if (path.isEmpty()) {
        // Default to the profile's last remote path, else remote home ("~").
        path = m_profile.lastRemotePath.isEmpty() ? QStringLiteral("~")
                                                   : m_profile.lastRemotePath;
    }
    // Normalize separators to POSIX and collapse redundant slashes, but keep a
    // leading "~" or "/" intact. QDir::cleanPath is purely lexical (no disk
    // access), which is exactly what we want for a remote path.
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    path = QDir::cleanPath(path);
    return path;
}

} // namespace remote
