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

#ifndef IACP_PROCESS_CHANNEL_H
#define IACP_PROCESS_CHANNEL_H

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>

// The transport seam under AcpConnection (D8). AcpConnection speaks JSON-RPC over
// a byte pipe; whether that pipe is a LOCAL child process (QProcess) or a process
// running on a REMOTE host over an SSH exec channel is hidden behind this
// interface. The JSON-RPC framing / dispatch logic in AcpConnection is identical
// for both — only the bytes' transport differs.
//
// Lifecycle: construct → start() (async; emits started() once the process is up)
// → write() frames / readyReadStdout(stderr) chunks flow → finished() on exit (or
// errorOccurred() on a spawn / transport failure). The channel is a QObject so it
// can be parented to (and destroyed with) its owning AcpConnection.
//
// The signal shapes intentionally mirror QProcess so the local adapter is a pure
// pass-through (zero behavioral change to local agent sessions): finished()
// carries (exitCode, QProcess::ExitStatus); errorOccurred() carries a
// QProcess::ProcessError plus a human-readable message (the SSH adapter maps its
// failures onto the closest QProcess::ProcessError so AcpConnection's existing
// error-classification path is unchanged).
class IAcpProcessChannel : public QObject
{
    Q_OBJECT

public:
    explicit IAcpProcessChannel(QObject *parent = nullptr) : QObject(parent) {}
    ~IAcpProcessChannel() override = default;

    // Begin spawning the agent process. Asynchronous: started() fires when the
    // process is live and ready to receive JSON-RPC frames; errorOccurred()
    // fires if it never comes up.
    virtual void start() = 0;

    // Feed bytes to the process's stdin. For the agent session this is a
    // newline-terminated JSON-RPC frame; the channel keeps stdin open for the
    // whole session (no EOF) — frames keep being written until the process exits.
    virtual void write(const QByteArray &bytes) = 0;

    // Terminate the process / tear down the exec channel.
    virtual void kill() = 0;

    // True between a successful start and finished()/kill(). Mirrors
    // (QProcess::state() != QProcess::NotRunning) for the local adapter.
    virtual bool isRunning() const = 0;

signals:
    // The process is up and stdin/stdout are usable (→ AcpConnection runs the
    // initialize handshake). Mirrors QProcess::started.
    void started();

    // A chunk of stdout / stderr arrived. AcpConnection appends stdout to its
    // frame reassembly buffer and routes stderr to the debug log — exactly as it
    // did when reading QProcess directly.
    void readyReadStdout(const QByteArray &chunk);
    void readyReadStderr(const QByteArray &chunk);

    // The process exited. exitStatus distinguishes a normal exit from a crash,
    // matching QProcess::finished(int, QProcess::ExitStatus).
    void finished(int exitCode, QProcess::ExitStatus exitStatus);

    // A spawn / transport error. `message` is already human-readable;
    // AcpConnection feeds it through AcpErrorClassifier exactly as before.
    void errorOccurred(QProcess::ProcessError error, const QString &message);
};

#endif // IACP_PROCESS_CHANNEL_H
