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

// Offline coverage of the RemoteFsBackend SFTP engine (D1), driven entirely by
// FakeSshTransport + SshSessionWorker's SFTP slots/serviceSftpForTest() — NO
// network, no sshd, deterministic. RemoteFsBackend is a thin UI-thread adapter:
// each readFileAsync/writeFileAsync/statAsync/readdirAsync mints a reqId,
// registers a callback, and posts requestSftp* onto this exact worker engine;
// the matching sftp*Done signal (which SshConnection relays 1:1 to the backend)
// resolves the callback. So exercising the worker's SFTP slots + signals
// exercises the backend's full data path WITHOUT dragging in the libssh2 /
// mbedTLS / CredentialStore stack that a live SshConnection would require
// (same offline-isolation rationale as test_ssh_session_worker).
//
// Asserts the normative D1 invariants:
//   - read: open → chunked drain → EOF, incl. partial/EAGAIN reassembly
//   - write: open → chunked write (incl. EAGAIN) → captured payload round-trip
//   - stat: dir vs file, size/mtime decode, absent path = exists=false / ok=true
//   - readdir: entry list with "."/".." filtered, Done terminates, EAGAIN resumes
//   - error injection: open-fail surfaces ok=false through the result signal
//   - ONE sftpInit across many ops (the one-channel-reuse invariant)

#include <QtTest>
#include <QByteArray>
#include <QList>
#include <QSignalSpy>
#include <QString>

#include <memory>

#include "remote/ISshTransport.h"
#include "remote/RemoteFsBackend.h" // D12 isTransientError (inline static, pure)
#include "remote/SshSessionWorker.h"

#include "FakeSshTransport.h"

using namespace remote;

// Drive the connect/auth handshake with agent auth (no secret needed).
static SshSessionWorker::ConnectParams agentParams()
{
    SshSessionWorker::ConnectParams p;
    p.host = QStringLiteral("host.example");
    p.port = 22;
    p.username = QStringLiteral("alice");
    p.authMethod = SshProfile::AuthMethod::Agent;
    return p;
}

// Build a worker driven to Ready, returning the worker + the observed raw fake.
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

static ISshTransport::SftpStatResult statOk(bool isDir, quint64 size, quint64 mtime)
{
    ISshTransport::SftpStatResult r;
    r.step = ISshTransport::Step::Ok;
    r.attrs.isDir = isDir;
    r.attrs.hasSize = true;
    r.attrs.size = size;
    r.attrs.hasMtime = true;
    r.attrs.mtime = mtime;
    return r;
}

static ISshTransport::SftpDirEntry dirEntry(const char *name, bool isDir, quint64 size)
{
    ISshTransport::SftpDirEntry e;
    e.kind = ISshTransport::SftpDirEntry::Kind::Entry;
    e.name = QByteArray(name);
    e.attrs.isDir = isDir;
    e.attrs.hasSize = true;
    e.attrs.size = size;
    return e;
}

static ISshTransport::SftpDirEntry dirAgain()
{
    ISshTransport::SftpDirEntry e;
    e.kind = ISshTransport::SftpDirEntry::Kind::Again;
    return e;
}

class TestRemoteFsBackend : public QObject
{
    Q_OBJECT

private slots:
    void read_wholeFile();
    void read_partialEagainReassembly();
    void read_openFailureSurfacesError();
    void write_roundTrip();
    void write_eagainThenCompletes();
    void stat_fileVsDir();
    void stat_absentPathNotAnError();
    void readdir_entriesAndDoneFiltersDots();
    void readdir_eagainResumes();
    void oneSftpInitAcrossManyOps();
    void bulkReadDoesNotBlockMetadataReaddir();

    // D12 read-only auto-retry: classifier coverage.
    void isTransientError_classification();
};

// --- read --------------------------------------------------------------------

void TestRemoteFsBackend::read_wholeFile()
{
    FakeSshTransport *f = nullptr;
    SshSessionWorker *worker = makeReadyWorker(&f);

    const QString path = QStringLiteral("/home/alice/notes.txt");
    f->sftpReadScript[path] = { dataChunk("Hello, "), dataChunk("world"), eofResult() };

    QSignalSpy readSpy(worker, &SshSessionWorker::sftpReadDone);
    worker->requestSftpRead(/*reqId=*/1, path); // services synchronously (all Ok)

    QCOMPARE(readSpy.count(), 1);
    const QList<QVariant> a = readSpy.first();
    QCOMPARE(a.at(0).toULongLong(), quint64(1));     // reqId echoed
    QVERIFY(a.at(1).toBool());                        // ok
    QCOMPARE(a.at(2).toByteArray(), QByteArrayLiteral("Hello, world"));
    QVERIFY(a.at(3).toString().isEmpty());            // no error

    // The reused SFTP session was opened exactly once.
    QCOMPARE(f->sftpInitCalls, 1);
    delete worker;
}

void TestRemoteFsBackend::read_partialEagainReassembly()
{
    FakeSshTransport *f = nullptr;
    SshSessionWorker *worker = makeReadyWorker(&f);

    const QString path = QStringLiteral("/var/log/app.log");
    // Chunk, then an explicit EAGAIN mid-stream, then the rest + EOF. The first
    // service pass reads "frag-1" then stalls on Again; a later socket edge
    // (serviceSftpForTest here) resumes and reassembles the whole payload.
    f->sftpReadScript[path] = {
        dataChunk("frag-1;"), againResult(),
        dataChunk("frag-2;"), dataChunk("frag-3"), eofResult()
    };

    QSignalSpy readSpy(worker, &SshSessionWorker::sftpReadDone);
    worker->requestSftpRead(/*reqId=*/7, path);

    // Stalled on the injected Again — not finished yet.
    QCOMPARE(readSpy.count(), 0);

    worker->serviceSftpForTest(); // resume on the next "edge"
    QCOMPARE(readSpy.count(), 1);
    const QList<QVariant> a = readSpy.first();
    QCOMPARE(a.at(0).toULongLong(), quint64(7));
    QVERIFY(a.at(1).toBool());
    QCOMPARE(a.at(2).toByteArray(), QByteArrayLiteral("frag-1;frag-2;frag-3"));

    QCOMPARE(f->sftpInitCalls, 1);
    delete worker;
}

void TestRemoteFsBackend::read_openFailureSurfacesError()
{
    FakeSshTransport *f = nullptr;
    SshSessionWorker *worker = makeReadyWorker(&f);

    const QString path = QStringLiteral("/root/secret");
    f->sftpOpenStepByPath[path] = ISshTransport::Step::Error; // e.g. permission denied

    QSignalSpy readSpy(worker, &SshSessionWorker::sftpReadDone);
    worker->requestSftpRead(/*reqId=*/3, path);

    QCOMPARE(readSpy.count(), 1);
    const QList<QVariant> a = readSpy.first();
    QCOMPARE(a.at(0).toULongLong(), quint64(3));
    QVERIFY(!a.at(1).toBool());              // ok == false
    QVERIFY(a.at(2).toByteArray().isEmpty());
    QVERIFY(!a.at(3).toString().isEmpty());  // a human reason is present
    delete worker;
}

// --- write -------------------------------------------------------------------

void TestRemoteFsBackend::write_roundTrip()
{
    FakeSshTransport *f = nullptr;
    SshSessionWorker *worker = makeReadyWorker(&f);

    const QString path = QStringLiteral("/home/alice/out.txt");
    const QByteArray payload = QByteArrayLiteral("the quick brown fox");

    QSignalSpy writeSpy(worker, &SshSessionWorker::sftpWriteDone);
    worker->requestSftpWrite(/*reqId=*/11, path, payload);

    QCOMPARE(writeSpy.count(), 1);
    const QList<QVariant> a = writeSpy.first();
    QCOMPARE(a.at(0).toULongLong(), quint64(11));
    QVERIFY(a.at(1).toBool());                 // ok
    QVERIFY(a.at(2).toString().isEmpty());

    // The fake exposes the captured bytes once the write handle is closed — a
    // full round-trip of the payload (no truncation, exact bytes).
    QCOMPARE(f->sftpWrittenByPath.value(path), payload);
    QCOMPARE(f->sftpInitCalls, 1);
    delete worker;
}

void TestRemoteFsBackend::write_eagainThenCompletes()
{
    FakeSshTransport *f = nullptr;
    SshSessionWorker *worker = makeReadyWorker(&f);

    const QString path = QStringLiteral("/home/alice/big.bin");
    const QByteArray payload = QByteArrayLiteral("0123456789abcdef");
    f->sftpWriteEagainsRemaining[path] = 1; // first chWrite EAGAINs, then accepts

    QSignalSpy writeSpy(worker, &SshSessionWorker::sftpWriteDone);
    worker->requestSftpWrite(/*reqId=*/12, path, payload);

    // EAGAIN on the first write attempt → not finished; write notifier armed so
    // the socket-writable edge resumes the transfer (D4 contract).
    QCOMPARE(writeSpy.count(), 0);
    QVERIFY(worker->writeNotifierEnabledForTest());

    worker->serviceSftpForTest(); // socket writable → drain the rest
    QCOMPARE(writeSpy.count(), 1);
    QVERIFY(writeSpy.first().at(1).toBool());
    QCOMPARE(f->sftpWrittenByPath.value(path), payload);
    delete worker;
}

// --- stat --------------------------------------------------------------------

void TestRemoteFsBackend::stat_fileVsDir()
{
    FakeSshTransport *f = nullptr;
    SshSessionWorker *worker = makeReadyWorker(&f);

    const QString dirPath = QStringLiteral("/home/alice/project");
    const QString filePath = QStringLiteral("/home/alice/project/main.cpp");
    f->sftpStatByPath[dirPath] = statOk(/*isDir=*/true, /*size=*/4096, /*mtime=*/1000);
    f->sftpStatByPath[filePath] = statOk(/*isDir=*/false, /*size=*/2048, /*mtime=*/1717000000ULL);

    QSignalSpy statSpy(worker, &SshSessionWorker::sftpStatDone);

    worker->requestSftpStat(/*reqId=*/21, dirPath);
    worker->requestSftpStat(/*reqId=*/22, filePath);
    QCOMPARE(statSpy.count(), 2);

    // sftpStatDone(reqId, ok, exists, isDir, size, mtimeSecs, error)
    const QList<QVariant> d = statSpy.at(0);
    QCOMPARE(d.at(0).toULongLong(), quint64(21));
    QVERIFY(d.at(1).toBool());   // ok
    QVERIFY(d.at(2).toBool());   // exists
    QVERIFY(d.at(3).toBool());   // isDir
    QCOMPARE(d.at(4).toLongLong(), qint64(4096));

    const QList<QVariant> file = statSpy.at(1);
    QCOMPARE(file.at(0).toULongLong(), quint64(22));
    QVERIFY(file.at(1).toBool());
    QVERIFY(file.at(2).toBool());
    QVERIFY(!file.at(3).toBool());                       // not a dir
    QCOMPARE(file.at(4).toLongLong(), qint64(2048));     // size
    QCOMPARE(file.at(5).toLongLong(), qint64(1717000000)); // mtime preserved
    delete worker;
}

void TestRemoteFsBackend::stat_absentPathNotAnError()
{
    FakeSshTransport *f = nullptr;
    SshSessionWorker *worker = makeReadyWorker(&f);

    // No script for this path → the fake returns Step::Error ("no such path").
    QSignalSpy statSpy(worker, &SshSessionWorker::sftpStatDone);
    worker->requestSftpStat(/*reqId=*/23, QStringLiteral("/nope/missing"));

    QCOMPARE(statSpy.count(), 1);
    const QList<QVariant> a = statSpy.first();
    QCOMPARE(a.at(0).toULongLong(), quint64(23));
    QVERIFY(a.at(1).toBool());   // ok == true: absence is not an I/O error
    QVERIFY(!a.at(2).toBool());  // exists == false
    delete worker;
}

// --- readdir -----------------------------------------------------------------

void TestRemoteFsBackend::readdir_entriesAndDoneFiltersDots()
{
    FakeSshTransport *f = nullptr;
    SshSessionWorker *worker = makeReadyWorker(&f);

    const QString path = QStringLiteral("/home/alice/project");
    // "." / ".." must be filtered out (matches local QDir::NoDotAndDotDot). Done
    // is synthesized by the fake when the scripted queue drains.
    f->sftpReaddirScript[path] = {
        dirEntry(".", true, 0),
        dirEntry("..", true, 0),
        dirEntry("README.md", false, 120),
        dirEntry("src", true, 4096),
    };

    QSignalSpy ddSpy(worker, &SshSessionWorker::sftpReaddirDone);
    worker->requestSftpReaddir(/*reqId=*/31, path);

    QCOMPARE(ddSpy.count(), 1);
    const QList<QVariant> a = ddSpy.first();
    QCOMPARE(a.at(0).toULongLong(), quint64(31));
    QVERIFY(a.at(1).toBool()); // ok
    const auto entries = a.at(2).value<QList<RemoteDirEntry>>();
    QCOMPARE(entries.size(), 2); // dots filtered
    QCOMPARE(entries.at(0).name, QStringLiteral("README.md"));
    QVERIFY(!entries.at(0).isDir);
    QCOMPARE(entries.at(0).size, qint64(120));
    QCOMPARE(entries.at(1).name, QStringLiteral("src"));
    QVERIFY(entries.at(1).isDir);
    QCOMPARE(f->sftpInitCalls, 1);
    delete worker;
}

void TestRemoteFsBackend::readdir_eagainResumes()
{
    FakeSshTransport *f = nullptr;
    SshSessionWorker *worker = makeReadyWorker(&f);

    const QString path = QStringLiteral("/srv/data");
    // An explicit Again mid-enumeration exercises the retry path: the first pass
    // collects "a.txt" then stalls; the next edge resumes and finishes.
    f->sftpReaddirScript[path] = {
        dirEntry("a.txt", false, 1),
        dirAgain(),
        dirEntry("b.txt", false, 2),
    };

    QSignalSpy ddSpy(worker, &SshSessionWorker::sftpReaddirDone);
    worker->requestSftpReaddir(/*reqId=*/32, path);
    QCOMPARE(ddSpy.count(), 0); // stalled on Again

    worker->serviceSftpForTest(); // resume → collect b.txt, then Done
    QCOMPARE(ddSpy.count(), 1);
    const auto entries = ddSpy.first().at(2).value<QList<RemoteDirEntry>>();
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(0).name, QStringLiteral("a.txt"));
    QCOMPARE(entries.at(1).name, QStringLiteral("b.txt"));
    delete worker;
}

// --- two-channel-reuse invariant (D1a) ----------------------------------------

void TestRemoteFsBackend::oneSftpInitAcrossManyOps()
{
    FakeSshTransport *f = nullptr;
    SshSessionWorker *worker = makeReadyWorker(&f);

    const QString readPath = QStringLiteral("/a/r.txt");
    const QString writePath = QStringLiteral("/a/w.txt");
    const QString statPath = QStringLiteral("/a/s.txt");
    const QString dirPath = QStringLiteral("/a");
    f->sftpReadScript[readPath] = { dataChunk("x"), eofResult() };
    f->sftpStatByPath[statPath] = statOk(false, 1, 1);
    f->sftpReaddirScript[dirPath] = { dirEntry("r.txt", false, 1) };

    QSignalSpy readSpy(worker, &SshSessionWorker::sftpReadDone);
    QSignalSpy writeSpy(worker, &SshSessionWorker::sftpWriteDone);
    QSignalSpy statSpy(worker, &SshSessionWorker::sftpStatDone);
    QSignalSpy ddSpy(worker, &SshSessionWorker::sftpReaddirDone);

    worker->requestSftpRead(101, readPath);
    worker->requestSftpWrite(102, writePath, QByteArrayLiteral("data"));
    worker->requestSftpStat(103, statPath);
    worker->requestSftpReaddir(104, dirPath);

    QCOMPARE(readSpy.count(), 1);
    QCOMPARE(writeSpy.count(), 1);
    QCOMPARE(statSpy.count(), 1);
    QCOMPARE(ddSpy.count(), 1);

    // D1a invariant: two SFTP sessions (one per lane) — bulk (read+write) and
    // metadata (stat+readdir). Each lane opens its channel once and reuses it
    // across all ops of that kind. Never a session/channel per op.
    QCOMPARE(f->sftpInitCalls, 2);
    delete worker;
}

// --- D1a: bulk read does NOT block metadata readdir --------------------------

void TestRemoteFsBackend::bulkReadDoesNotBlockMetadataReaddir()
{
    FakeSshTransport *f = nullptr;
    SshSessionWorker *worker = makeReadyWorker(&f);

    // Script a bulk read that stalls on EAGAIN after the first chunk — it stays
    // "in flight" on the bulk lane, simulating a large file transfer.
    const QString bulkPath = QStringLiteral("/home/alice/bigfile.bin");
    f->sftpReadScript[bulkPath] = {
        dataChunk("chunk1;"), againResult()
        // No EOF yet — the bulk op is stuck mid-transfer.
    };

    // Script a metadata readdir that completes immediately (no EAGAIN).
    const QString metaDir = QStringLiteral("/home/alice/project");
    f->sftpReaddirScript[metaDir] = {
        dirEntry("main.cpp", false, 1024),
        dirEntry("lib", true, 4096),
    };

    QSignalSpy readSpy(worker, &SshSessionWorker::sftpReadDone);
    QSignalSpy ddSpy(worker, &SshSessionWorker::sftpReaddirDone);

    // Issue the bulk read first — it will stall on EAGAIN after "chunk1;".
    worker->requestSftpRead(/*reqId=*/50, bulkPath);
    QCOMPARE(readSpy.count(), 0); // stalled on EAGAIN — still in flight

    // Issue the metadata readdir WHILE the bulk read is in flight.
    worker->requestSftpReaddir(/*reqId=*/51, metaDir);

    // The metadata op completes immediately (its lane is independent) even though
    // the bulk lane is stalled. This is the D1a non-blocking guarantee.
    QCOMPARE(ddSpy.count(), 1);
    const auto entries = ddSpy.first().at(2).value<QList<RemoteDirEntry>>();
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(0).name, QStringLiteral("main.cpp"));
    QCOMPARE(entries.at(1).name, QStringLiteral("lib"));

    // The bulk read is still NOT done (no more data scripted yet).
    QCOMPARE(readSpy.count(), 0);

    // Now provide the remaining bulk data and resume. The read handle is already
    // open (the op is mid-Transfer), so the remaining reads are injected into the
    // live handle state (reads are bound at open time, not re-read from the script).
    for (auto it = f->sftpHandles.begin(); it != f->sftpHandles.end(); ++it) {
        if (it->path == bulkPath && !it->forWrite) {
            it->reads = { dataChunk("chunk2"), eofResult() };
            break;
        }
    }
    worker->serviceSftpForTest();
    QCOMPARE(readSpy.count(), 1);
    QVERIFY(readSpy.first().at(1).toBool()); // ok
    QCOMPARE(readSpy.first().at(2).toByteArray(), QByteArrayLiteral("chunk1;chunk2"));

    // Two SFTP sessions total: one for bulk, one for metadata.
    QCOMPARE(f->sftpInitCalls, 2);
    delete worker;
}

// --- D12 read-only auto-retry: transient/permanent classification -------------
//
// RemoteFsBackend's read-only ops (readFileAsync/statAsync/readdirAsync) auto-
// retry up to 2 additional times (3 attempts total) with a 200ms->400ms backoff
// on TRANSIENT failures, because they are idempotent; writeFileAsync (mutating)
// never retries. The retry decision keys off the pure static classifier
// RemoteFsBackend::isTransientError, asserted directly here.
//
// The full re-issue/backoff PATH cannot be exercised offline: instantiating a
// RemoteFsBackend pulls in SshConnection -> Libssh2Transport -> libssh2/mbedTLS,
// which this offline suite (FakeSshTransport + SshSessionWorker only) does not
// link. That path is verified by (a) code review of the attempt-N helpers in
// RemoteFsBackend.cpp and (b) the existing worker-level transient-failure
// coverage -- the lane-init failure surfaces "Could not open SFTP session"
// (see FakeSshTransport::sftpInitFailsRemaining), exactly the TRANSIENT reason
// the backend retries on. isTransientError is inline + static + pure, so it is
// fully unit-testable here without any connection.
void TestRemoteFsBackend::isTransientError_classification()
{
    // TRANSIENT (connection/session-level) -> retry. Case-insensitive substrings
    // matching SshSessionWorker::failSftpOp / enterConnectionLost reasons.
    QVERIFY(RemoteFsBackend::isTransientError(QStringLiteral("Not connected")));
    QVERIFY(RemoteFsBackend::isTransientError(QStringLiteral("Disconnected")));
    QVERIFY(RemoteFsBackend::isTransientError(QStringLiteral("Could not open SFTP session")));
    QVERIFY(RemoteFsBackend::isTransientError(QStringLiteral("SSH connection lost")));
    QVERIFY(RemoteFsBackend::isTransientError(
        QStringLiteral("SSH connection lost (keepalive timeout)")));
    // Case-insensitivity is load-bearing (reasons are tr()-able, casing varies).
    QVERIFY(RemoteFsBackend::isTransientError(QStringLiteral("not CONNECTED")));
    QVERIFY(RemoteFsBackend::isTransientError(QStringLiteral("CONNECTION LOST")));

    // PERMANENT (permission / I/O / no-such-file) -> NO retry. These are the
    // per-op failSftpOp reasons that would just fail again on replay.
    QVERIFY(!RemoteFsBackend::isTransientError(
        QStringLiteral("Could not open remote file for reading")));
    QVERIFY(!RemoteFsBackend::isTransientError(QStringLiteral("Remote read failed")));
    QVERIFY(!RemoteFsBackend::isTransientError(
        QStringLiteral("Remote directory listing failed")));
    QVERIFY(!RemoteFsBackend::isTransientError(QStringLiteral("Could not open remote directory")));
    QVERIFY(!RemoteFsBackend::isTransientError(QStringLiteral("No SSH connection")));
    QVERIFY(!RemoteFsBackend::isTransientError(QString())); // empty = success path, never retried
}

QTEST_MAIN(TestRemoteFsBackend)

#include "test_remote_fs_backend.moc"
