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

#ifndef TESTS_FAKESSHTRANSPORT_H
#define TESTS_FAKESSHTRANSPORT_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

#include "remote/ISshTransport.h"

// Scriptable ISshTransport fake shared across every offline SSH test (the
// SshSessionWorker state machine and all the Phase-2..4 remote subsystems —
// RemoteFsBackend, RemoteFileSystemModel, RemoteGitProcessRunner, the ACP exec
// transport — drive against this so CI needs no real sshd). Every knob is
// public so each test arranges exactly the scenario it needs. socketFd()
// returns -1 so the worker creates no real QSocketNotifier and pumpForTest()
// drives the pump deterministically.
//
// Two scriptable layers sit on top of the Phase-1 channel scripting:
//   * exec replay  — execOrShell captures the command string per channel;
//     stdout is scripted via readScript, stderr via readStderrScript, exit via
//     exitStatus (mirrors how a real exec channel surfaces those streams).
//   * SFTP         — readdir/stat/read/write are scripted per path with full
//     partial-read / EAGAIN / error injection (see the SFTP block below).
class FakeSshTransport : public remote::ISshTransport
{
public:
    // ---- connect / auth scripting (Phase 1) --------------------------------
    Step connectStep = Step::Ok;
    Step handshakeStep = Step::Ok;
    Step authStep = Step::Ok;
    QByteArray fakeHostKey = QByteArrayLiteral("FAKE-HOST-KEY-BLOB");

    // recorded auth
    int authPasswordCalls = 0;
    int authPublicKeyCalls = 0;
    int authAgentCalls = 0;
    QString lastAuthUser;
    QString lastAuthPassword;

    // ---- channel-open scripting (Phase 1) ----------------------------------
    Step openStep = Step::Ok; // Again to simulate EAGAIN on open
    QList<int> openedIds;     // transport ids assigned, in order
    int nextId = 1;
    int closedCount = 0;
    QList<int> closedIds;

    // per-transportId stdout read script (popped front; empty -> Again)
    QHash<int, QList<ReadResult>> readScript;
    QHash<int, int> readCalls;

    // per-transportId write script: number of EAGAINs before it accepts in full
    QHash<int, int> writeEagainsRemaining;
    int writeCalls = 0;
    // per-transportId captured channel-write bytes (stdin feed assertion for the
    // exec/git path — the worker writes stdinPayload here before draining).
    QHash<int, QByteArray> chWritten;

    QHash<int, int> exitStatus;

    // ---- exec replay (D6/D8) -----------------------------------------------
    // execOrShell return value (default Ok keeps the Phase-1 worker tests
    // happy — they only need a channel to come up). Command strings are
    // captured so the git/ACP exec tests can assert the exact `cd .. && ..`.
    Step execStep = Step::Ok;
    QHash<int, QString> execCommands; // last command seen per transport id
    QList<QString> execCommandLog;    // every command, in call order
    QString lastExecCommand;

    // per-transportId stderr read script (popped front; empty -> Again)
    QHash<int, QList<ReadResult>> readStderrScript;
    QHash<int, int> readStderrCalls;

    // ---- SFTP scripting (D1/D11) -------------------------------------------
    // The fake models ONE reused SFTP session (sftpInit/sftpShutdown) exactly
    // like the production transport, so the one-channel-reuse invariant is
    // observable: sftpInitCalls counts how many times a session was actually
    // established (a test asserts it stays 1 across many ops).
    Step sftpInitStep = Step::Ok;
    int sftpInitCalls = 0;   // times a NEW session was established
    bool sftpSessionUp = false;
    int sftpShutdownCalls = 0;

    // File-open scripting, keyed by path. sftpOpenStep lets a test force
    // Again/Error on the next open of any path; per-path overrides win.
    Step sftpOpenStep = Step::Ok;
    QHash<QString, Step> sftpOpenStepByPath;

    // Read scripting: queued chunks per PATH, bound to the handle at open time
    // (so a test scripts content by path, not by the opaque handle id). Empty
    // queue -> Again, letting tests inject partial reads then an explicit EOF.
    QHash<QString, QList<ReadResult>> sftpReadScript;

    // Write scripting: per-path EAGAIN budget + captured bytes. After a write
    // handle is closed, sftpWrittenByPath holds everything written to it, so a
    // round-trip test can assert the payload.
    QHash<QString, int> sftpWriteEagainsRemaining;
    QHash<QString, QByteArray> sftpWrittenByPath;

    // Readdir scripting: queued entries per dir PATH (Done is synthesized when
    // the queue drains, but a test can also enqueue an explicit Again/Error to
    // exercise the retry / fatal paths mid-enumeration).
    QHash<QString, QList<SftpDirEntry>> sftpReaddirScript;

    // Stat scripting: result per path (default: Error == "no such path").
    QHash<QString, SftpStatResult> sftpStatByPath;

    // Recorded activity for assertions.
    QList<QString> sftpOpenedPaths;
    QList<QString> sftpOpendirPaths;
    QList<QString> sftpStatPaths;
    int sftpCloseCalls = 0;

    // Per-handle runtime state (private to the fake; tests script by path).
    struct SftpHandleState
    {
        QString path;
        bool forWrite = false;
        bool isDir = false;
        QList<ReadResult> reads;     // copied from sftpReadScript at open
        QList<SftpDirEntry> entries; // copied from sftpReaddirScript at opendir
        QByteArray written;          // accumulated by sftpWrite
    };
    QHash<int, SftpHandleState> sftpHandles;
    int nextSftpHandle = 1;

    Step connectSocket(const QString &, int) override { return connectStep; }
    Step handshake() override { return handshakeStep; }
    QByteArray hostKey() const override { return fakeHostKey; }

    Step authPassword(const QString &username, const QString &password) override
    {
        ++authPasswordCalls;
        lastAuthUser = username;
        lastAuthPassword = password;
        return authStep;
    }
    Step authPublicKey(const QString &username, const QString &, const QString &) override
    {
        ++authPublicKeyCalls;
        lastAuthUser = username;
        return authStep;
    }
    Step authAgent(const QString &username) override
    {
        ++authAgentCalls;
        lastAuthUser = username;
        return authStep;
    }

    OpenResult openChannel() override
    {
        OpenResult r;
        if (openStep != Step::Ok) {
            r.step = openStep;
            return r;
        }
        r.step = Step::Ok;
        r.channelId = nextId++;
        openedIds.append(r.channelId);
        return r;
    }
    Step requestPty(int, const QByteArray &, int, int) override { return Step::Ok; }
    Step resizePty(int, int, int) override { return Step::Ok; }
    Step execOrShell(int channelId, const QString &command) override
    {
        // Capture the command (empty == interactive shell) so the exec-channel
        // tests can replay/assert it; default return keeps Phase-1 tests green.
        execCommands[channelId] = command;
        execCommandLog.append(command);
        lastExecCommand = command;
        return execStep;
    }

    qint64 chWrite(int channelId, const QByteArray &bytes) override
    {
        ++writeCalls;
        int &eagains = writeEagainsRemaining[channelId];
        if (eagains > 0) {
            --eagains;
            return kWriteAgain;
        }
        chWritten[channelId].append(bytes); // capture stdin feed
        return bytes.size(); // accept in full
    }

    ReadResult chRead(int channelId) override
    {
        ++readCalls[channelId];
        QList<ReadResult> &q = readScript[channelId];
        if (q.isEmpty()) {
            ReadResult r;
            r.again = true;
            return r;
        }
        return q.takeFirst();
    }

    ReadResult chReadStderr(int channelId) override
    {
        ++readStderrCalls[channelId];
        QList<ReadResult> &q = readStderrScript[channelId];
        if (q.isEmpty()) {
            ReadResult r;
            r.again = true;
            return r;
        }
        return q.takeFirst();
    }

    int chExitStatus(int channelId) override { return exitStatus.value(channelId, 0); }
    void closeChannel(int channelId) override { ++closedCount; closedIds.append(channelId); }
    qintptr socketFd() const override { return -1; } // no real QSocketNotifier in tests
    void disconnect() override {}

    // PLACEHOLDER_SFTP_METHODS
    SftpOpenResult sftpInit() override
    {
        SftpOpenResult r;
        if (sftpInitStep != Step::Ok) {
            r.step = sftpInitStep;
            return r;
        }
        if (!sftpSessionUp) {
            sftpSessionUp = true;
            ++sftpInitCalls; // only counts a genuinely-new session
        }
        r.step = Step::Ok;
        return r;
    }

    void sftpShutdown() override
    {
        ++sftpShutdownCalls;
        sftpSessionUp = false;
        sftpHandles.clear();
    }

    SftpOpenResult sftpOpen(const QString &path, bool forWrite) override
    {
        SftpOpenResult r;
        sftpOpenedPaths.append(path);
        const Step step = sftpOpenStepByPath.value(path, sftpOpenStep);
        if (step != Step::Ok) {
            r.step = step;
            return r;
        }
        SftpHandleState st;
        st.path = path;
        st.forWrite = forWrite;
        st.isDir = false;
        if (!forWrite) {
            st.reads = sftpReadScript.value(path); // bind content at open
        }
        const int id = nextSftpHandle++;
        sftpHandles.insert(id, st);
        r.step = Step::Ok;
        r.handleId = id;
        return r;
    }

    ReadResult sftpRead(int handleId) override
    {
        ReadResult out;
        auto it = sftpHandles.find(handleId);
        if (it == sftpHandles.end()) {
            out.error = true;
            return out;
        }
        QList<ReadResult> &q = it->reads;
        if (q.isEmpty()) {
            out.again = true; // tests enqueue an explicit eof to finish
            return out;
        }
        return q.takeFirst();
    }

    qint64 sftpWrite(int handleId, const QByteArray &bytes) override
    {
        auto it = sftpHandles.find(handleId);
        if (it == sftpHandles.end()) {
            return kWriteError;
        }
        int &eagains = sftpWriteEagainsRemaining[it->path];
        if (eagains > 0) {
            --eagains;
            return kWriteAgain;
        }
        it->written.append(bytes);
        return bytes.size(); // accept in full
    }

    void sftpClose(int handleId) override
    {
        ++sftpCloseCalls;
        auto it = sftpHandles.find(handleId);
        if (it == sftpHandles.end()) {
            return;
        }
        if (it->forWrite) {
            sftpWrittenByPath[it->path] = it->written; // expose for assertions
        }
        sftpHandles.erase(it);
    }

    SftpOpenResult sftpOpendir(const QString &path) override
    {
        SftpOpenResult r;
        sftpOpendirPaths.append(path);
        const Step step = sftpOpenStepByPath.value(path, sftpOpenStep);
        if (step != Step::Ok) {
            r.step = step;
            return r;
        }
        SftpHandleState st;
        st.path = path;
        st.isDir = true;
        st.entries = sftpReaddirScript.value(path); // bind listing at opendir
        const int id = nextSftpHandle++;
        sftpHandles.insert(id, st);
        r.step = Step::Ok;
        r.handleId = id;
        return r;
    }

    SftpDirEntry sftpReaddir(int handleId) override
    {
        SftpDirEntry out;
        auto it = sftpHandles.find(handleId);
        if (it == sftpHandles.end()) {
            out.kind = SftpDirEntry::Kind::Error;
            return out;
        }
        QList<SftpDirEntry> &q = it->entries;
        if (q.isEmpty()) {
            out.kind = SftpDirEntry::Kind::Done; // drained -> end of stream
            return out;
        }
        return q.takeFirst();
    }

    void sftpClosedir(int handleId) override { sftpClose(handleId); }

    SftpStatResult sftpStat(const QString &path) override
    {
        sftpStatPaths.append(path);
        SftpStatResult def;
        def.step = Step::Error; // default: path absent / not scripted
        return sftpStatByPath.value(path, def);
    }
};

#endif // TESTS_FAKESSHTRANSPORT_H
