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

#include "SshAcpProcessChannel.h"

#include <utility>

SshAcpProcessChannel::SshAcpProcessChannel(IAcpExecHost *host,
                                           QString command,
                                           QStringList args,
                                           QString workingDirectory,
                                           QObject *parent)
    : IAcpProcessChannel(parent)
    , m_host(host)
    , m_command(std::move(command))
    , m_args(std::move(args))
    , m_workingDirectory(std::move(workingDirectory))
{
    if (m_host) {
        connect(m_host, &IAcpExecHost::execStdout, this, &SshAcpProcessChannel::onExecStdout);
        connect(m_host, &IAcpExecHost::execStderr, this, &SshAcpProcessChannel::onExecStderr);
        connect(m_host, &IAcpExecHost::execDone, this, &SshAcpProcessChannel::onExecDone);
    }
}

SshAcpProcessChannel::~SshAcpProcessChannel()
{
    // Tear the exec channel down on the remote host if still streaming.
    if (m_running && m_host && m_reqId != 0) {
        m_host->execCancel(m_reqId);
    }
}

void SshAcpProcessChannel::start()
{
    if (!m_host) {
        // No transport behind us — fail closed (no local fallback). Mirrors a
        // QProcess FailedToStart so AcpConnection's error path is unchanged.
        emit errorOccurred(QProcess::FailedToStart,
                           QStringLiteral("No SSH connection for remote agent"));
        return;
    }
    const QString command = buildRemoteCommand(m_workingDirectory, m_command, m_args);
    // Open the exec channel with NO one-shot stdin payload — the session feeds
    // frames incrementally via write()/execWrite over the channel's lifetime.
    m_reqId = m_host->execStart(command, QByteArray());
    m_running = true;
    // The exec channel is usable as soon as execStart returns (the worker opens
    // it asynchronously, but writes posted now are buffered until it is live —
    // see SshSessionWorker's streamed-stdin path). Signal "started" so
    // AcpConnection runs the initialize handshake, matching QProcess::started.
    emit started();
}

void SshAcpProcessChannel::write(const QByteArray &bytes)
{
    if (m_running && m_host && m_reqId != 0) {
        m_host->execWrite(m_reqId, bytes);
    }
}

void SshAcpProcessChannel::kill()
{
    if (m_running && m_host && m_reqId != 0) {
        m_host->execCancel(m_reqId);
    }
    m_running = false;
}

void SshAcpProcessChannel::onExecStdout(quint64 reqId, const QByteArray &chunk)
{
    if (reqId != m_reqId || !m_running) {
        return;
    }
    emit readyReadStdout(chunk);
}

void SshAcpProcessChannel::onExecStderr(quint64 reqId, const QByteArray &chunk)
{
    if (reqId != m_reqId || !m_running) {
        return;
    }
    emit readyReadStderr(chunk);
}

void SshAcpProcessChannel::onExecDone(quint64 reqId, int exitStatus)
{
    if (reqId != m_reqId || !m_running) {
        return;
    }
    m_running = false;
    m_reqId = 0;
    // exitStatus < 0 signals a transport-level failure (connection loss / open
    // failure) per the SshSessionWorker exec engine; map it to a crash exit so
    // AcpConnection surfaces the "agent exited" state. A normal remote exit
    // (>= 0) maps to NormalExit with the code passed through 1:1.
    const QProcess::ExitStatus status =
        (exitStatus < 0) ? QProcess::CrashExit : QProcess::NormalExit;
    emit finished(exitStatus, status);
}
