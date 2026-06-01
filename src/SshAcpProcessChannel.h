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

#ifndef SSH_ACP_PROCESS_CHANNEL_H
#define SSH_ACP_PROCESS_CHANNEL_H

#include <QByteArray>
#include <QPointer>
#include <QString>
#include <QStringList>

#include "IAcpExecHost.h"
#include "IAcpProcessChannel.h"

// IAcpProcessChannel over an SSH exec channel (D8). It runs the agent on the
// remote host by exec'ing
//
//     cd <cwd> && exec <command> <args...>
//
// (every token POSIX single-quoted) over one exec channel of the multiplexed SSH
// connection, then speaks JSON-RPC over that channel's stdin/stdout exactly like
// the local QProcess path. start() opens the channel via IAcpExecHost::execStart;
// write() appends a frame via execWrite (stdin stays open for the whole session —
// the agent reads frames continuously, so no EOF is needed); kill() / destruction
// tears the channel down via execCancel. stdout/stderr/exit are surfaced through
// the host's reqId-keyed relay signals and re-emitted on the IAcpProcessChannel
// surface, so AcpConnection's framing/dispatch is unchanged.
//
// The host is NOT owned (it is the SshConnection facade, owned by the registry).
// All libssh2 work happens on the connection's worker thread; this object lives
// on the UI thread and only posts queued requests / receives queued results.
class SshAcpProcessChannel : public IAcpProcessChannel
{
    Q_OBJECT

public:
    SshAcpProcessChannel(IAcpExecHost *host,
                         QString command,
                         QStringList args,
                         QString workingDirectory,
                         QObject *parent = nullptr);
    ~SshAcpProcessChannel() override;

    void start() override;
    void write(const QByteArray &bytes) override;
    void kill() override;
    bool isRunning() const override { return m_running; }

    // POSIX single-quote one token (close/escape/reopen for embedded quotes) —
    // identical rule to RemoteGitProcessRunner::shellQuote. Defined inline so
    // tests can assert it without linking the SSH stack.
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

    // Build `cd <cwd> && exec <command> <args...>` with every token shell-quoted.
    // An empty cwd skips the `cd` prefix (runs in the channel's default dir, e.g.
    // the remote home). `exec` replaces the shell so the agent is the channel's
    // process group leader and signals/exit map straight through.
    static QString buildRemoteCommand(const QString &cwd, const QString &command,
                                      const QStringList &args)
    {
        QString cmd;
        if (!cwd.isEmpty()) {
            cmd += QStringLiteral("cd ") + shellQuote(cwd) + QStringLiteral(" && ");
        }
        cmd += QStringLiteral("exec ") + shellQuote(command);
        for (const QString &arg : args) {
            cmd += QLatin1Char(' ') + shellQuote(arg);
        }
        return cmd;
    }

private:
    void onExecStdout(quint64 reqId, const QByteArray &chunk);
    void onExecStderr(quint64 reqId, const QByteArray &chunk);
    void onExecDone(quint64 reqId, int exitStatus);

    QPointer<IAcpExecHost> m_host; // not owned
    QString m_command;
    QStringList m_args;
    QString m_workingDirectory;
    quint64 m_reqId = 0; // 0 = not started / finished
    bool m_running = false;
};

#endif // SSH_ACP_PROCESS_CHANNEL_H
