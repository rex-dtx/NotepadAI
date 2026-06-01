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

#ifndef SSH_CONNECTION_EXEC_HOST_H
#define SSH_CONNECTION_EXEC_HOST_H

#include <QByteArray>
#include <QPointer>
#include <QString>

#include "IAcpExecHost.h"
#include "remote/SshConnection.h"

// Production IAcpExecHost: a thin forwarder onto a live remote::SshConnection's
// streaming-exec surface. It exists so SshAcpProcessChannel can depend on the
// light IAcpExecHost interface instead of the whole SSH stack — only the app
// (which already links the SSH stack) ever instantiates this. Owned by the
// channel it backs (set its parent to the channel) so it shares its lifetime.
class SshConnectionExecHost : public IAcpExecHost
{
    Q_OBJECT

public:
    explicit SshConnectionExecHost(remote::SshConnection *connection, QObject *parent = nullptr)
        : IAcpExecHost(parent)
        , m_connection(connection)
    {
        if (m_connection) {
            // 1:1 relay of the connection's reqId-keyed exec result signals.
            connect(m_connection, &remote::SshConnection::execStdout,
                    this, &IAcpExecHost::execStdout);
            connect(m_connection, &remote::SshConnection::execStderr,
                    this, &IAcpExecHost::execStderr);
            connect(m_connection, &remote::SshConnection::execDone,
                    this, &IAcpExecHost::execDone);
        }
    }

    quint64 execStart(const QString &command, const QByteArray &stdinPayload) override
    {
        // ACP agent sessions are always long-lived (the channel stays open for
        // the session's lifetime), so tag explicitly for the FIX-2 admission budget.
        return m_connection
                   ? m_connection->execStart(command, stdinPayload, remote::ExecKind::LongLived)
                   : 0;
    }

    void execWrite(quint64 reqId, const QByteArray &bytes) override
    {
        if (m_connection) {
            m_connection->execWrite(reqId, bytes);
        }
    }

    void execCancel(quint64 reqId) override
    {
        if (m_connection) {
            m_connection->execCancel(reqId);
        }
    }

private:
    QPointer<remote::SshConnection> m_connection; // not owned (registry owns it)
};

#endif // SSH_CONNECTION_EXEC_HOST_H
