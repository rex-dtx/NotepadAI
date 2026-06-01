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

#ifndef IACP_EXEC_HOST_H
#define IACP_EXEC_HOST_H

#include <QByteArray>
#include <QObject>
#include <QString>

// The minimal exec-channel contract SshAcpProcessChannel needs (D8). It is the
// exact streaming-exec surface of remote::SshConnection — execStart / execWrite /
// execCancel + the execStdout / execStderr / execDone relay signals — extracted
// behind an interface so the ACP exec adapter depends on this small QObject
// instead of the whole SSH stack (libssh2 / mbedTLS / worker thread). The
// production host (SshConnectionExecHost) forwards to a live SshConnection; tests
// drive a fake host wired straight to an SshSessionWorker + FakeSshTransport, so
// the adapter's full data path is exercised offline (same isolation rationale as
// RemoteGitProcessRunner's worker-level test).
//
// execStart mints a process-wide-unique reqId and returns it; the *result signals
// echo that reqId so a host shared by many channels routes each stream to the
// right adapter. execWrite appends bytes to an in-flight op's stdin (the agent
// session keeps stdin open for its whole life — JSON-RPC frames keep flowing;
// there is no EOF). execCancel tears the channel down with no execDone.
class IAcpExecHost : public QObject
{
    Q_OBJECT

public:
    explicit IAcpExecHost(QObject *parent = nullptr) : QObject(parent) {}
    ~IAcpExecHost() override = default;

    virtual quint64 execStart(const QString &command, const QByteArray &stdinPayload) = 0;
    virtual void execWrite(quint64 reqId, const QByteArray &bytes) = 0;
    virtual void execCancel(quint64 reqId) = 0;

signals:
    void execStdout(quint64 reqId, const QByteArray &chunk);
    void execStderr(quint64 reqId, const QByteArray &chunk);
    void execDone(quint64 reqId, int exitStatus);
};

#endif // IACP_EXEC_HOST_H
