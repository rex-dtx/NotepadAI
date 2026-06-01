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

#ifndef REMOTE_REMOTEGITPROCESSRUNNER_H
#define REMOTE_REMOTEGITPROCESSRUNNER_H

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include "git/GitProcessRunner.h" // IGitProcessRunner

class QTimer;

namespace remote {

class SshConnection;

// IGitProcessRunner over an SSH exec channel (D6). Behavior-identical to the
// local GitProcessRunner from each caller's POV: same run() signature, same
// cancel()/isRunning()/setMaxOutputBytes()/cancelAsync(), same kExit* sentinels
// (reused from GitProcessRunner), same progressLine signal.
//
// Instead of spawning a local `git` QProcess, it opens ONE exec channel on the
// multiplexed SshConnection running:
//
//     cd <cwd-shell-quoted> && git <argv...-shell-quoted>
//
// stdout is streamed back over the channel and accumulated; stderr is honored
// per readErrAsProgress (true → emitted line-by-line as progressLine, matching
// the local runner's git-progress behavior; false → captured into the err
// payload). stdinPayload, if non-empty, is fed to the channel before reading.
// The exit code comes back via the channel exit-status (libssh2
// channel_get_exit_status), mapped 1:1 to the Callback's exit argument.
//
// One exec channel per invocation, multiplexed on the existing connection — no
// new TCP, no lock, no per-op handshake. Matches the local one-process-per-run
// shape. All libssh2 work happens on the connection's worker thread; this object
// lives on the UI thread and only posts queued requests / receives queued
// results (no locks), resolving them by the reqId minted by SshConnection.
class RemoteGitProcessRunner : public QObject, public IGitProcessRunner
{
    Q_OBJECT

public:
    explicit RemoteGitProcessRunner(SshConnection *connection, QObject *parent = nullptr);
    ~RemoteGitProcessRunner() override;

    void run(const QString &cwd,
             const QStringList &argv,
             const QByteArray &stdinPayload,
             int timeoutMs,
             bool readErrAsProgress,
             Callback cb) override;
    void cancel() override;
    bool isRunning() const override { return m_running; }

    void cancelAsync() override;
    QObject *asQObject() override { return this; }

    void setMaxOutputBytes(qint64 bytes) override { m_maxOutputBytes = bytes; }
    qint64 maxOutputBytes() const override { return m_maxOutputBytes; }

    // --- command construction (pure; unit-tested directly) -------------------
    // Single-quote a token for POSIX sh: wrap in '...' and escape any embedded
    // single quote as '\'' (close-quote, escaped-quote, reopen-quote). Safe for
    // arbitrary bytes — no shell metacharacter can survive single-quoting.
    // Defined inline so tests can assert it without linking the SSH stack.
    static QString shellQuote(const QString &token)
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
    // Build the remote shell command: `cd <cwd> && git <argv...>`, every token
    // shell-quoted. An empty cwd skips the `cd` prefix (runs in the channel's
    // default dir, e.g. the remote home), matching a local run() with empty cwd.
    static QString buildRemoteCommand(const QString &cwd, const QStringList &argv)
    {
        QString cmd;
        if (!cwd.isEmpty()) {
            cmd += QStringLiteral("cd ") + shellQuote(cwd) + QStringLiteral(" && ");
        }
        cmd += QStringLiteral("git");
        for (const QString &arg : argv) {
            cmd += QLatin1Char(' ') + shellQuote(arg);
        }
        return cmd;
    }

signals:
    void progressLine(const QString &line);

private:
    void onExecStdout(quint64 reqId, const QByteArray &chunk);
    void onExecStderr(quint64 reqId, const QByteArray &chunk);
    void onExecDone(quint64 reqId, int exitStatus);
    void finish(int exit, const QByteArray &out, const QByteArray &err);
    void reset();
    void onTimeout();

    QPointer<SshConnection> m_connection; // not owned (registry owns it)
    quint64 m_reqId = 0;                   // 0 = no in-flight op
    bool m_running = false;
    bool m_cancelled = false;
    bool m_truncated = false;
    bool m_progressMode = false;
    Callback m_cb;
    QByteArray m_stdoutBuf;
    QByteArray m_stderrBuf;
    QByteArray m_stderrLineBuf; // for line-by-line progress emission
    qint64 m_maxOutputBytes = 0; // 0 = unlimited
    QTimer *m_timeout = nullptr;
};

} // namespace remote

#endif // REMOTE_REMOTEGITPROCESSRUNNER_H
