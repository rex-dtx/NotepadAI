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

#include "LocalAcpProcessChannel.h"

#include <QProcess>

#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

LocalAcpProcessChannel::LocalAcpProcessChannel(AcpProtocol::SpawnArgv argv,
                                               const QProcessEnvironment &env,
                                               QString workingDirectory,
                                               QObject *parent)
    : IAcpProcessChannel(parent)
    , m_argv(std::move(argv))
    , m_env(env)
    , m_workingDirectory(std::move(workingDirectory))
{
}

LocalAcpProcessChannel::~LocalAcpProcessChannel()
{
    // Mirror the prior AcpConnection destructor: a still-running agent is killed
    // and briefly joined so the child does not outlive the session.
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

void LocalAcpProcessChannel::start()
{
    m_process = new QProcess(this);
    m_process->setWorkingDirectory(m_workingDirectory);
    m_process->setProcessEnvironment(m_env);

#ifdef Q_OS_WIN
    m_process->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *a) {
            if (a) {
                a->flags |= CREATE_NEW_PROCESS_GROUP;
            }
        });
#endif

    m_process->setProgram(m_argv.program);
#ifdef Q_OS_WIN
    if (!m_argv.nativeArgumentsLine.isEmpty()) {
        m_process->setNativeArguments(m_argv.nativeArgumentsLine);
    } else
#endif
    {
        m_process->setArguments(m_argv.arguments);
    }

    // Re-emit QProcess signals through the IAcpProcessChannel surface. stdout /
    // stderr are read here (the only place that touches the QProcess) and pushed
    // out as chunks so AcpConnection's reassembly is transport-agnostic.
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        emit readyReadStdout(m_process->readAllStandardOutput());
    });
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        emit readyReadStderr(m_process->readAllStandardError());
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        // errorString() is the same value AcpConnection read off m_process before.
        emit errorOccurred(err, m_process ? m_process->errorString() : QString());
    });
    connect(m_process,
            static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, &IAcpProcessChannel::finished);
    connect(m_process, &QProcess::started, this, &IAcpProcessChannel::started);

    m_process->start();
}

void LocalAcpProcessChannel::write(const QByteArray &bytes)
{
    if (m_process) {
        m_process->write(bytes);
    }
}

void LocalAcpProcessChannel::kill()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
    }
}

bool LocalAcpProcessChannel::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}
