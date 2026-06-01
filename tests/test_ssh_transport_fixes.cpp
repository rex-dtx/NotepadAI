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

// Offline coverage of the D0 transport-core fixes (FIX-1..FIX-4), driven
// entirely by FakeSshTransport + SshSessionWorker test hooks. NO network, no
// sshd, deterministic. Asserts the normative design invariants:
//   group 1: ChannelBusy re-queues not drops, connection survives (FIX-1)
//   group 2: unified channel counter spans PTY+exec+SFTP vs cap 8 (FIX-2)
//   group 3: starvation/reserve — git-exec admitted while 6th long-lived queues (FIX-2)
//   group 4: keepalive miss -> connection-lost; inbound resets miss count (FIX-3)
//   group 5: connect timeout + cancel preempts immediately (FIX-4)

#include <QtTest>
#include <QSignalSpy>
#include <QThread>

#include <memory>

#include "remote/ISshTransport.h"
#include "remote/SshSessionWorker.h"

#include "FakeSshTransport.h"

using namespace remote;

// --- helpers ----------------------------------------------------------------

// Password-auth connect params (mirrors test_ssh_session_worker).
static SshSessionWorker::ConnectParams passwordParams(const QString &pw)
{
    SshSessionWorker::ConnectParams p;
    p.host = QStringLiteral("host.example");
    p.port = 22;
    p.username = QStringLiteral("alice");
    p.authMethod = SshProfile::AuthMethod::Password;
    p.passwordProvider = [pw]() { return pw; };
    return p;
}

// Build a worker already driven to Ready, returning the worker + the (observed)
// raw fake pointer. cap controls the unified channel cap (FIX-2).
static SshSessionWorker *makeReadyWorker(FakeSshTransport **fakeOut, int cap = 8)
{
    auto fake = std::make_unique<FakeSshTransport>();
    *fakeOut = fake.get();
    auto *worker = new SshSessionWorker(std::move(fake), cap);
    worker->startConnect(passwordParams(QStringLiteral("pw")));
    worker->acceptHostKey(); // gate → drives to Ready
    return worker;
}

// Bring the single reused SFTP session up (so unifiedChannelCountForTest counts
// it as a reserved channel). Issues one stat against an unscripted path — the
// stat resolves immediately (exists=false), but ensureSftpInited has run, so
// m_sftpInited is latched true.
static void bringSftpUp(SshSessionWorker *worker)
{
    worker->requestSftpStat(900, QStringLiteral("/probe"));
    worker->serviceSftpForTest();
}

// Advance the real wall clock without pumping the event loop, so the worker's
// auto-armed maintenance QTimer never fires and only the explicit *ForTest hooks
// drive retries (backoff uses QDateTime::currentMSecsSinceEpoch()).
static void spinWallClock(unsigned long ms)
{
    QThread::msleep(ms);
}

class TestSshTransportFixes : public QObject
{
    Q_OBJECT

private slots:
    void fix1_channelBusyReQueuesAndBacksOff();
    void fix2_unifiedCounterSpansAllKindsAndCaps();
    void fix2_shortLivedReserveNeverStarved();
    void fix3_keepaliveMissLosesConnection();
    void fix4_connectTimeoutAndCancelPreempt();
};

// --- group 1: ChannelBusy re-queues not drops (FIX-1) -----------------------

void TestSshTransportFixes::fix1_channelBusyReQueuesAndBacksOff()
{
    FakeSshTransport *f = nullptr;
    SshSessionWorker *worker = makeReadyWorker(&f, /*cap=*/8);

    // Script: openChannel returns ChannelBusy twice, then Ok on the third call.
    f->openBusyCount = 2;

    QSignalSpy openedSpy(worker, &SshSessionWorker::channelOpened);
    QSignalSpy disconnectedSpy(worker, &SshSessionWorker::disconnected);

    // Request a PTY channel. The first pump inside requestOpenChannel calls
    // admitPending (promotes to Opening) then advanceChannelSetup (first
    // openChannel → ChannelBusy). The channel stays in Opening, not dropped.
    worker->requestOpenChannel(1, /*wantPty=*/true, QByteArray(), 80, 24, QString());

    QCOMPARE(openedSpy.count(), 0);
    QCOMPARE(disconnectedSpy.count(), 0);
    // The channel is live (Opening phase counts against the cap) — not dropped.
    QCOMPARE(worker->liveChannelCountForTest(), 1);
    // It was promoted out of the queue into Opening, so queued == 0.
    QCOMPARE(worker->queuedChannelCountForTest(), 0);
    // First openChannel call happened.
    QCOMPARE(f->openChannelCalls, 1);

    // Immediate tick: backoff window (250ms) has NOT elapsed → no retry.
    worker->tickMaintenanceForTest();
    QCOMPARE(f->openChannelCalls, 1); // no extra call
    QCOMPARE(openedSpy.count(), 0);

    // Wait past the first backoff (250ms) and tick → second attempt (ChannelBusy
    // again, backoff doubles to 500ms).
    spinWallClock(300);
    worker->tickMaintenanceForTest();
    QCOMPARE(f->openChannelCalls, 2); // retried once
    QCOMPARE(openedSpy.count(), 0);   // still busy
    QCOMPARE(disconnectedSpy.count(), 0); // connection alive

    // Wait past the second backoff (500ms) and tick → third attempt succeeds.
    spinWallClock(550);
    worker->tickMaintenanceForTest();
    QCOMPARE(f->openChannelCalls, 3);
    QCOMPARE(openedSpy.count(), 1);
    QCOMPARE(openedSpy.first().at(0).toInt(), 1); // logical id 1 opened
    QCOMPARE(disconnectedSpy.count(), 0); // connection survived the whole time
    QCOMPARE(worker->state(), SshSessionWorker::State::Ready);

    delete worker;
}

// --- group 2: unified channel counter + budget (FIX-2) ----------------------

void TestSshTransportFixes::fix2_unifiedCounterSpansAllKindsAndCaps()
{
    FakeSshTransport *f = nullptr;
    SshSessionWorker *worker = makeReadyWorker(&f, /*cap=*/8);

    // Part A: verify the unified counter sums PTY + exec + SFTP.
    // Bring SFTP up (counts as 1 reserved channel).
    bringSftpUp(worker);
    QCOMPARE(worker->unifiedChannelCountForTest(), 1); // SFTP only

    // Open one PTY channel (long-lived).
    worker->requestOpenChannel(1, /*wantPty=*/false, QByteArray(), 80, 24, QString());
    QCOMPARE(worker->liveChannelCountForTest(), 1);
    QCOMPARE(worker->unifiedChannelCountForTest(), 2); // PTY + SFTP

    // Open a git-exec (short-lived) and an acp-exec (long-lived).
    worker->requestExec(101, QStringLiteral("git status"), QByteArray(), ExecKind::ShortLived);
    worker->requestExec(102, QStringLiteral("claude"), QByteArray(), ExecKind::LongLived);
    // Both should be admitted (budget has room). Service them to Streaming.
    worker->serviceExecForTest();
    QCOMPARE(worker->unifiedChannelCountForTest(), 4); // 1 PTY + 2 exec + 1 SFTP

    // The legacy PTY-only counter still reports 1 (proving the old counter was
    // incomplete — the whole point of FIX-2).
    QCOMPARE(worker->liveChannelCountForTest(), 1);

    // Part B: saturate the dynamic budget and verify admission stops.
    // Dynamic budget = cap(8) - sftpReserved(2) = 6. Currently 3 dynamic
    // channels live (1 PTY + 2 exec). Add 3 more long-lived execs to fill it.
    worker->requestExec(103, QStringLiteral("agent2"), QByteArray(), ExecKind::LongLived);
    worker->requestExec(104, QStringLiteral("agent3"), QByteArray(), ExecKind::LongLived);
    worker->requestExec(105, QStringLiteral("agent4"), QByteArray(), ExecKind::LongLived);
    worker->serviceExecForTest();
    // liveLong = 1 PTY + 4 LongLived exec = 5 (maxLong). liveDynamic = 6.
    QCOMPARE(worker->unifiedChannelCountForTest(), 7); // 6 dynamic + 1 SFTP

    // Attempt one more long-lived exec → should be QUEUED (budget full).
    worker->requestExec(106, QStringLiteral("agent5"), QByteArray(), ExecKind::LongLived);
    worker->serviceExecForTest();
    // Unified count stays at 7 — the 6th long-lived was not admitted.
    QCOMPARE(worker->unifiedChannelCountForTest(), 7);
    QVERIFY(worker->queuedChannelCountForTest() >= 1);

    // State remains Ready (no connection loss from budget enforcement).
    QCOMPARE(worker->state(), SshSessionWorker::State::Ready);

    delete worker;
}

// --- group 3: short-lived reserve never starved (FIX-2) ---------------------

void TestSshTransportFixes::fix2_shortLivedReserveNeverStarved()
{
    FakeSshTransport *f = nullptr;
    SshSessionWorker *worker = makeReadyWorker(&f, /*cap=*/8);

    // Bring the SFTP session up. It counts as a reserved channel; the budget
    // reserves kSftpReserved(2) slots regardless of the actual SFTP channel
    // count (the D1a bulk/metadata split that fills both is a later task — for
    // now one SFTP channel is live, but the 2-slot reservation already holds).
    bringSftpUp(worker);

    // Occupy all 5 long-lived dynamic slots with acp-exec (LongLived) channels.
    // dynamicBudget = cap(8) - sftpReserved(2) = 6; maxLong = 5; the 6th slot is
    // the short-lived reserve.
    for (int i = 0; i < 5; ++i) {
        worker->requestExec(200 + i, QStringLiteral("agent"), QByteArray(), ExecKind::LongLived);
    }
    worker->serviceExecForTest();
    // 5 long-lived + 1 SFTP live; the long-lived budget is now saturated.
    QCOMPARE(worker->unifiedChannelCountForTest(), 6);

    QSignalSpy queuedSpy(worker, &SshSessionWorker::channelQueued);
    QSignalSpy openedSpy(worker, &SshSessionWorker::channelOpened);

    // Queue a 6th LONG-LIVED opener (a PTY channel — channelQueued is emitted for
    // PTY openers) AND a git-exec (ShortLived). The git-exec must be admitted
    // into the reserved short-lived slot; the 6th long-lived must stay queued.
    worker->requestOpenChannel(/*logicalId=*/6, /*wantPty=*/true, QByteArray(), 80, 24, QString());
    worker->requestExec(/*reqId=*/300, QStringLiteral("git status"), QByteArray(),
                        ExecKind::ShortLived);
    worker->serviceExecForTest();

    // The git-exec was admitted: unified count rose to 7 (5 long exec + 1 git +
    // 1 SFTP). The git-exec is NOT starved despite 5 long-lived agents holding
    // every long-lived slot.
    QCOMPARE(worker->unifiedChannelCountForTest(), 7);

    // The 6th long-lived (PTY id 6) is still queued, NOT opened.
    QVERIFY(worker->queuedChannelCountForTest() >= 1);
    bool sawOpen6 = false;
    for (const QList<QVariant> &args : openedSpy) {
        if (args.at(0).toInt() == 6) sawOpen6 = true;
    }
    QVERIFY(!sawOpen6); // PTY id 6 never opened — still waiting

    // channelQueued fired for the waiting long-lived PTY (the UX banner trigger).
    bool sawQueued6 = false;
    for (const QList<QVariant> &args : queuedSpy) {
        if (args.at(0).toInt() == 6) sawQueued6 = true;
    }
    QVERIFY(sawQueued6);

    QCOMPARE(worker->state(), SshSessionWorker::State::Ready);

    delete worker;
}

// --- group 4: keepalive miss -> connection lost (FIX-3) ---------------------

void TestSshTransportFixes::fix3_keepaliveMissLosesConnection()
{
    // Scenario A: no inbound activity → 2 consecutive misses → connection lost.
    {
        FakeSshTransport *f = nullptr;
        SshSessionWorker *worker = makeReadyWorker(&f, /*cap=*/8);
        QCOMPARE(worker->state(), SshSessionWorker::State::Ready);

        QSignalSpy disconnectedSpy(worker, &SshSessionWorker::disconnected);

        // Tick 1: no inbound since entering Ready → miss=1 (not lost yet). A
        // keepalive probe is sent (returns 15, healthy).
        worker->tickKeepaliveForTest();
        QCOMPARE(disconnectedSpy.count(), 0);
        QVERIFY(f->keepaliveCalls >= 1);
        QCOMPARE(worker->state(), SshSessionWorker::State::Ready);

        // Tick 2: still no inbound → miss=2 → connection lost (~30s in prod).
        worker->tickKeepaliveForTest();
        QCOMPARE(disconnectedSpy.count(), 1);
        QCOMPARE(worker->state(), SshSessionWorker::State::Disconnected);

        delete worker;
    }

    // Scenario B: a markInboundActivity between ticks resets the miss count, so
    // the connection is NOT lost.
    {
        FakeSshTransport *f = nullptr;
        SshSessionWorker *worker = makeReadyWorker(&f, /*cap=*/8);

        QSignalSpy disconnectedSpy(worker, &SshSessionWorker::disconnected);

        // Tick 1: no inbound → miss=1.
        worker->tickKeepaliveForTest();
        QCOMPARE(disconnectedSpy.count(), 0);

        // Inbound activity arrives (e.g. a keepalive reply / channel data).
        worker->markInboundActivityForTest();

        // Tick 2: inbound was seen → miss resets to 0 (no loss).
        worker->tickKeepaliveForTest();
        QCOMPARE(disconnectedSpy.count(), 0);
        QCOMPARE(worker->state(), SshSessionWorker::State::Ready);

        // Tick 3: no inbound again → miss=1 (still alive — proves the counter
        // really was reset, not merely deferred).
        worker->tickKeepaliveForTest();
        QCOMPARE(disconnectedSpy.count(), 0);
        QCOMPARE(worker->state(), SshSessionWorker::State::Ready);

        delete worker;
    }

    // Scenario C: a fatal send error (keepaliveReturn < 0) loses the connection
    // immediately on the next probe.
    {
        FakeSshTransport *f = nullptr;
        SshSessionWorker *worker = makeReadyWorker(&f, /*cap=*/8);
        f->keepaliveReturn = -1; // simulate a fatal socket error during the send

        // Pretend inbound just happened so the miss path is NOT what trips it —
        // the send-error path must be what loses the connection.
        worker->markInboundActivityForTest();

        QSignalSpy disconnectedSpy(worker, &SshSessionWorker::disconnected);
        worker->tickKeepaliveForTest();
        QCOMPARE(disconnectedSpy.count(), 1);
        QCOMPARE(worker->state(), SshSessionWorker::State::Disconnected);

        delete worker;
    }
}

// --- group 5: non-blocking connect timeout + cancel (FIX-4) -----------------

void TestSshTransportFixes::fix4_connectTimeoutAndCancelPreempt()
{
    // Scenario A: connect-in-progress → connectSocket returns Again repeatedly
    // (state stays ConnectingSocket); a queued requestDisconnect (Cancel)
    // preempts immediately → Disconnected promptly (no hang).
    {
        auto fake = std::make_unique<FakeSshTransport>();
        FakeSshTransport *f = fake.get();
        f->connectInProgress = true; // non-blocking ::connect still waiting

        SshSessionWorker worker(std::move(fake));
        QSignalSpy stateSpy(&worker, &SshSessionWorker::stateChanged);

        worker.startConnect(passwordParams(QStringLiteral("pw")));
        // The non-blocking connect did not complete → still ConnectingSocket.
        QCOMPARE(worker.state(), SshSessionWorker::State::ConnectingSocket);
        QCOMPARE(f->connectSocketCalls, 1);

        // The maintenance timer re-polls while connecting; driving it manually
        // keeps returning Again (the connect has not become writable yet).
        worker.tickMaintenanceForTest();
        worker.tickMaintenanceForTest();
        worker.tickMaintenanceForTest();
        QCOMPARE(worker.state(), SshSessionWorker::State::ConnectingSocket); // no hang, no progress
        QVERIFY(f->connectSocketCalls >= 4); // re-polled each tick, never blocked

        // Cancel: a queued requestDisconnect preempts immediately while still
        // connecting → Disconnected promptly (the pump never blocked on connect).
        const int statesBeforeCancel = stateSpy.count();
        worker.requestDisconnect();
        QCOMPARE(worker.state(), SshSessionWorker::State::Disconnected);
        // The cancel drove a fresh state transition (→ Disconnected) immediately.
        QVERIFY(stateSpy.count() > statesBeforeCancel);
    }

    // Scenario B: the worker honors Step::Error → connection-lost when the
    // (timed-out) connect flips to Error. This models the transport-enforced 15s
    // wall-clock deadline surfacing as Step::Error → "Could not reach host".
    {
        auto fake = std::make_unique<FakeSshTransport>();
        FakeSshTransport *f = fake.get();
        f->connectInProgress = true;

        SshSessionWorker worker(std::move(fake));
        QSignalSpy disconnectedSpy(&worker, &SshSessionWorker::disconnected);

        worker.startConnect(passwordParams(QStringLiteral("pw")));
        QCOMPARE(worker.state(), SshSessionWorker::State::ConnectingSocket);
        QCOMPARE(disconnectedSpy.count(), 0);

        // The deadline elapses: the transport's connectSocket now reports Error.
        f->connectInProgress = false;
        f->connectStep = ISshTransport::Step::Error;

        worker.tickMaintenanceForTest(); // re-poll → Error → connection lost
        QCOMPARE(disconnectedSpy.count(), 1);
        QCOMPARE(worker.state(), SshSessionWorker::State::Disconnected);
    }
}

QTEST_MAIN(TestSshTransportFixes)

#include "test_ssh_transport_fixes.moc"


