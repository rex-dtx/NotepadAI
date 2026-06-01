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

// Offline coverage of the remote git path (D6), in two layers — both driven by
// FakeSshTransport, NO network, no sshd, deterministic:
//
//   1. RemoteGitProcessRunner::buildRemoteCommand / shellQuote — the pure
//      argv → `cd <cwd> && git <argv...>` command construction with POSIX
//      single-quoting (the runner's own static, no SSH stack needed).
//   2. The SshSessionWorker exec engine (requestExec + serviceExecForTest +
//      execStdoutChunk/execStderrChunk/execDone), which is the worker half of
//      RemoteGitProcessRunner: the runner is a thin reqId→callback adapter over
//      these exact slots/signals (relayed 1:1 by SshConnection), so exercising
//      the worker exercises the runner's full data path WITHOUT the libssh2 /
//      mbedTLS / CredentialStore stack a live SshConnection needs (same
//      offline-isolation rationale as test_remote_fs_backend).
//
// Asserts: argv → exec command string (cd prefix + quoting), stdout capture &
// reassembly, stderr captured distinctly, exit-status mapping (zero & non-zero),
// stdin feed reaching the channel, EAGAIN resume, and cancel() tearing the
// channel down with no execDone.

#include <QtTest>
#include <QByteArray>
#include <QSignalSpy>
#include <QString>
#include <QStringList>

#include <memory>

#include "remote/ISshTransport.h"
#include "remote/RemoteGitProcessRunner.h"
#include "remote/SshSessionWorker.h"

#include "FakeSshTransport.h"

using namespace remote;

static SshSessionWorker::ConnectParams agentParams()
{
    SshSessionWorker::ConnectParams p;
    p.host = QStringLiteral("host.example");
    p.port = 22;
    p.username = QStringLiteral("alice");
    p.authMethod = SshProfile::AuthMethod::Agent;
    return p;
}

static SshSessionWorker *makeReadyWorker(FakeSshTransport **fakeOut)
{
    auto fake = std::make_unique<FakeSshTransport>();
    *fakeOut = fake.get();
    auto *worker = new SshSessionWorker(std::move(fake));
    worker->startConnect(agentParams());
    worker->acceptHostKey(); // host-key gate → drives to Ready
    return worker;
}

static ISshTransport::ReadResult dataChunk(const char *s)
{
    ISshTransport::ReadResult r;
    r.data = QByteArray(s);
    return r;
}

static ISshTransport::ReadResult eofResult()
{
    ISshTransport::ReadResult r;
    r.eof = true;
    return r;
}

static ISshTransport::ReadResult againResult()
{
    ISshTransport::ReadResult r;
    r.again = true;
    return r;
}

class TestRemoteGitRunner : public QObject
{
    Q_OBJECT

private slots:
    // command construction (pure)
    void command_cdPrefixAndQuoting();
    void command_emptyCwdSkipsCd();
    void command_embeddedSingleQuoteEscaped();

    // worker exec engine
    void exec_stdoutCaptureAndExitZero();
    void exec_stderrCapturedDistinctly();
    void exec_nonZeroExitMapped();
    void exec_stdinFedToChannel();
    void exec_eagainResumes();
    void exec_cancelClosesChannelNoDone();
};

// --- command construction -----------------------------------------------------

void TestRemoteGitRunner::command_cdPrefixAndQuoting()
{
    const QString cmd = RemoteGitProcessRunner::buildRemoteCommand(
        QStringLiteral("/home/alice/my project"),
        { QStringLiteral("status"), QStringLiteral("--porcelain=v2") });
    // cwd is single-quoted (space-safe); git + each arg single-quoted.
    QCOMPARE(cmd,
             QStringLiteral("cd '/home/alice/my project' && git 'status' '--porcelain=v2'"));
}

void TestRemoteGitRunner::command_emptyCwdSkipsCd()
{
    const QString cmd = RemoteGitProcessRunner::buildRemoteCommand(
        QString(), { QStringLiteral("rev-parse"), QStringLiteral("HEAD") });
    QCOMPARE(cmd, QStringLiteral("git 'rev-parse' 'HEAD'"));
}

void TestRemoteGitRunner::command_embeddedSingleQuoteEscaped()
{
    // A token containing a single quote must close/escape/reopen: '\''.
    QCOMPARE(RemoteGitProcessRunner::shellQuote(QStringLiteral("a'b")),
             QStringLiteral("'a'\\''b'"));
    // Format strings full of control bytes survive single-quoting unharmed.
    const QString fmt = QStringLiteral("--pretty=format:%H%x1f%s");
    QCOMPARE(RemoteGitProcessRunner::shellQuote(fmt),
             QLatin1Char('\'') + fmt + QLatin1Char('\''));
}

// --- worker exec engine -------------------------------------------------------

void TestRemoteGitRunner::exec_stdoutCaptureAndExitZero()
{
    FakeSshTransport *f = nullptr;
    SshSessionWorker *worker = makeReadyWorker(&f);

    // First exec channel gets transport id 1 (fake's nextId starts at 1).
    f->readScript[1] = { dataChunk("On branch "), dataChunk("main"), eofResult() };
    f->exitStatus[1] = 0;

    QSignalSpy outSpy(worker, &SshSessionWorker::execStdoutChunk);
    QSignalSpy doneSpy(worker, &SshSessionWorker::execDone);

    const QString cmd =
        RemoteGitProcessRunner::buildRemoteCommand(QStringLiteral("/repo"),
                                                   { QStringLiteral("status") });
    worker->requestExec(/*reqId=*/1, cmd, QByteArray()); // services synchronously

    // The exact `cd ... && git ...` string reached the transport's exec.
    QCOMPARE(f->execCommandLog.size(), 1);
    QCOMPARE(f->lastExecCommand, QStringLiteral("cd '/repo' && git 'status'"));

    // stdout chunks streamed as-read; reassembled by the runner side.
    QByteArray out;
    for (const auto &call : outSpy) {
        QCOMPARE(call.at(0).toULongLong(), quint64(1));
        out += call.at(1).toByteArray();
    }
    QCOMPARE(out, QByteArrayLiteral("On branch main"));

    QCOMPARE(doneSpy.count(), 1);
    QCOMPARE(doneSpy.first().at(0).toULongLong(), quint64(1)); // reqId echoed
    QCOMPARE(doneSpy.first().at(1).toInt(), 0);                 // exit 0
    delete worker;
}

void TestRemoteGitRunner::exec_stderrCapturedDistinctly()
{
    FakeSshTransport *f = nullptr;
    SshSessionWorker *worker = makeReadyWorker(&f);

    f->readScript[1] = { eofResult() };                       // no stdout
    f->readStderrScript[1] = { dataChunk("fatal: not a git repository"), eofResult() };
    f->exitStatus[1] = 128;

    QSignalSpy errSpy(worker, &SshSessionWorker::execStderrChunk);
    QSignalSpy doneSpy(worker, &SshSessionWorker::execDone);

    worker->requestExec(/*reqId=*/5, QStringLiteral("git 'status'"), QByteArray());

    QByteArray err;
    for (const auto &call : errSpy) {
        QCOMPARE(call.at(0).toULongLong(), quint64(5));
        err += call.at(1).toByteArray();
    }
    QCOMPARE(err, QByteArrayLiteral("fatal: not a git repository"));
    QCOMPARE(doneSpy.count(), 1);
    QCOMPARE(doneSpy.first().at(1).toInt(), 128);
    delete worker;
}

void TestRemoteGitRunner::exec_nonZeroExitMapped()
{
    FakeSshTransport *f = nullptr;
    SshSessionWorker *worker = makeReadyWorker(&f);

    f->readScript[1] = { eofResult() };
    f->exitStatus[1] = 1;

    QSignalSpy doneSpy(worker, &SshSessionWorker::execDone);
    worker->requestExec(/*reqId=*/9, QStringLiteral("git 'diff' '--quiet'"), QByteArray());

    QCOMPARE(doneSpy.count(), 1);
    QCOMPARE(doneSpy.first().at(0).toULongLong(), quint64(9));
    QCOMPARE(doneSpy.first().at(1).toInt(), 1); // exit-status passed through 1:1
    delete worker;
}

void TestRemoteGitRunner::exec_stdinFedToChannel()
{
    FakeSshTransport *f = nullptr;
    SshSessionWorker *worker = makeReadyWorker(&f);

    f->readScript[1] = { dataChunk("ok"), eofResult() };
    f->exitStatus[1] = 0;

    const QByteArray stdinPayload = QByteArrayLiteral("patch-bytes-on-stdin\n");
    QSignalSpy doneSpy(worker, &SshSessionWorker::execDone);
    worker->requestExec(/*reqId=*/12, QStringLiteral("git 'apply'"), stdinPayload);

    // The stdin payload was written to the exec channel (id 1) before draining.
    QCOMPARE(f->chWritten.value(1), stdinPayload);
    QCOMPARE(doneSpy.count(), 1);
    QCOMPARE(doneSpy.first().at(1).toInt(), 0);
    delete worker;
}

void TestRemoteGitRunner::exec_eagainResumes()
{
    FakeSshTransport *f = nullptr;
    SshSessionWorker *worker = makeReadyWorker(&f);

    // A chunk, then an explicit EAGAIN, then the rest + EOF. The first pass reads
    // "frag-1" and stalls; the next socket edge (serviceExecForTest) resumes.
    f->readScript[1] = {
        dataChunk("frag-1;"), againResult(),
        dataChunk("frag-2"), eofResult()
    };
    f->exitStatus[1] = 0;

    QSignalSpy outSpy(worker, &SshSessionWorker::execStdoutChunk);
    QSignalSpy doneSpy(worker, &SshSessionWorker::execDone);
    worker->requestExec(/*reqId=*/15, QStringLiteral("git 'log'"), QByteArray());

    // Stalled on the injected Again — not finished yet.
    QCOMPARE(doneSpy.count(), 0);

    worker->serviceExecForTest(); // resume on the next "edge"
    QCOMPARE(doneSpy.count(), 1);

    QByteArray out;
    for (const auto &call : outSpy) out += call.at(1).toByteArray();
    QCOMPARE(out, QByteArrayLiteral("frag-1;frag-2"));
    QCOMPARE(doneSpy.first().at(1).toInt(), 0);
    delete worker;
}

void TestRemoteGitRunner::exec_cancelClosesChannelNoDone()
{
    FakeSshTransport *f = nullptr;
    SshSessionWorker *worker = makeReadyWorker(&f);

    // No stdout script → chRead returns Again, so the op stays in-flight
    // (Streaming, never EOF) until cancelled.
    QSignalSpy doneSpy(worker, &SshSessionWorker::execDone);
    worker->requestExec(/*reqId=*/21, QStringLiteral("git 'log'"), QByteArray());
    QCOMPARE(doneSpy.count(), 0);          // still streaming
    QCOMPARE(f->openedIds.size(), 1);      // a channel was opened
    const int chId = f->openedIds.first();

    worker->requestExecCancel(/*reqId=*/21);
    // The channel was torn down and NO execDone is emitted (the runner already
    // resolved its own callback in cancel()).
    QVERIFY(f->closedIds.contains(chId));
    QCOMPARE(doneSpy.count(), 0);
    delete worker;
}

QTEST_MAIN(TestRemoteGitRunner)

#include "test_remote_git_runner.moc"
