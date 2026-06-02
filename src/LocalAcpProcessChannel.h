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

#ifndef LOCAL_ACP_PROCESS_CHANNEL_H
#define LOCAL_ACP_PROCESS_CHANNEL_H

#include <QByteArray>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include "AcpProtocol.h"
#include "IAcpProcessChannel.h"

class QProcess;

// Local IAcpProcessChannel — a thin wrapper around the exact QProcess usage
// AcpConnection had before the D8 refactor. Behavior-IDENTICAL to the prior code:
// same QProcessEnvironment, same program/arguments (or nativeArgumentsLine on
// Windows), same CREATE_NEW_PROCESS_GROUP modifier, same kill-on-destroy. The
// only change is that AcpConnection now reaches the process through this object
// instead of owning the QProcess directly — so a local agent session is
// byte-for-byte unchanged.
class LocalAcpProcessChannel : public IAcpProcessChannel
{
    Q_OBJECT

public:
    // Takes the already-resolved spawn descriptor + environment + cwd that
    // AcpConnection computed (so all spawn logging stays in AcpConnection,
    // unchanged). On Windows, argv.nativeArgumentsLine (when non-empty) is
    // applied via setNativeArguments, mirroring the prior inline code.
    LocalAcpProcessChannel(AcpProtocol::SpawnArgv argv,
                           const QProcessEnvironment &env,
                           QString workingDirectory,
                           QObject *parent = nullptr);
    ~LocalAcpProcessChannel() override;

    void start() override;
    void write(const QByteArray &bytes) override;
    void kill() override;
    bool isRunning() const override;

private:
    QProcess *m_process = nullptr;
    AcpProtocol::SpawnArgv m_argv;
    QProcessEnvironment m_env;
    QString m_workingDirectory;
};

#endif // LOCAL_ACP_PROCESS_CHANNEL_H
