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


// Async editor open path (D4 / task 12.5). Drives ScintillaNext::createShell +
// loadInto over a FAKE remote IFileSystemBackend (no SSH, no SshConnection): a
// tiny backend whose readFileAsync DEFERS its callback into a pending queue we
// flush by hand, so we can assert the "still loading" window precisely.
//
// Invariants asserted:
//   1. createShell() returns an editor that is isFile() + isRemote() + carries
//      the ssh:// URI identity IMMEDIATELY (before any bytes), in Loading state.
//      Because it is isFile(), the DockedEditor::initialEditor() reject test
//      (isTemporary || isFile || canUndo || canRedo) would reject it — it can
//      NEVER be mistaken for a pristine "New X" scratch tab and closed mid-load.
//   2. loadInto() fills the buffer from the backend's bytes (BOM-stripped) and
//      flips to Loaded + emits loaded() + sets the save point.
//   3. Ordering: opening a SECOND file while the first is still "loading" does
//      not close/reuse the loading tab — the first stays Loading + isFile().
//   4. URI identity: createShell stamps remoteUri()/getFilePath() to the URI;
//      two shells for the same URI compare equal, a third differs — the
//      EditorManager dedupe key.


#include <QtTest>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

#include "ScintillaNext.h"
#include "remote/IFileSystemBackend.h"
#include "remote/SshProfile.h"


// A fake remote backend: isRemote()==true, but readFileAsync queues the callback
// + the path it was asked to read, to be fired later by flush(). writeFileAsync
// records the last write and fires its callback immediately (success). The
// synchronous overrides fail-closed (a real remote backend cannot read on the
// UI thread) — exactly like RemoteFsBackend.
class FakeRemoteBackend : public remote::IFileSystemBackend
{
public:
    using remote::IFileSystemBackend::IFileSystemBackend;

    bool isRemote() const override { return true; }

    // Content the next flush() will deliver for a given path. If a path has no
    // scripted content, the read resolves as a failure.
    QHash<QString, QByteArray> contents;
    bool nextReadFails = false;
    QString nextReadError;

    struct Pending {
        QString path;
        ReadCallback cb;
    };
    QList<Pending> pendingReads;

    // Last write captured (for the save test, if needed).
    QString lastWritePath;
    QByteArray lastWriteData;
    bool failNextWrite = false;

    void readFileAsync(const QString &path, ReadCallback cb) override
    {
        pendingReads.append({path, std::move(cb)});
    }

    void writeFileAsync(const QString &path, const QByteArray &data, WriteCallback cb) override
    {
        lastWritePath = path;
        lastWriteData = data;
        if (cb) cb(!failNextWrite, failNextWrite ? QStringLiteral("forced") : QString());
    }

    // Resolve every queued read in FIFO order using the scripted contents map.
    void flush()
    {
        const QList<Pending> batch = pendingReads;
        pendingReads.clear();
        for (const Pending &p : batch) {
            if (nextReadFails) {
                if (p.cb) p.cb(false, QByteArray(), nextReadError);
                continue;
            }
            const auto it = contents.constFind(p.path);
            if (it == contents.constEnd()) {
                if (p.cb) p.cb(false, QByteArray(), QStringLiteral("no such file"));
            } else {
                if (p.cb) p.cb(true, it.value(), QString());
            }
        }
    }

    int pendingCount() const { return pendingReads.size(); }

    // --- fail-closed synchronous overrides ----------------------------------
    QByteArray readFile(const QString &, bool *ok) override { if (ok) *ok = false; return {}; }
    bool writeFile(const QString &, const QByteArray &) override { return false; }
    remote::FileStat stat(const QString &) override { return {}; }
    QStringList readdir(const QString &) override { return {}; }
};


// Replicates DockedEditor::initialEditor()'s per-editor reject predicate without
// constructing a CDockManager: a shell is a reusable scratch tab ONLY if it is
// not temporary, not a file, and has no undo/redo. We assert a loading remote
// shell is rejected (because it isFile()).
static bool wouldBeRejectedAsScratchTab(ScintillaNext *e)
{
    return e->isTemporary() || e->isFile() || e->canUndo() || e->canRedo();
}


class TestAsyncEditorLoad : public QObject
{
    Q_OBJECT

private slots:
    void shellHasFileAndUriIdentityImmediately();
    void shellIsNotMistakenForScratchTab();
    void loadIntoFillsBufferFromBackend();
    void loadIntoStripsUtf8Bom();
    void secondOpenDoesNotDisturbLoadingTab();
    void readFailureEntersErrorStateThenRetrySucceeds();
    void uriIsTheDedupeIdentity();
};


void TestAsyncEditorLoad::shellHasFileAndUriIdentityImmediately()
{
    FakeRemoteBackend backend;
    const QString uri = remote::formatSshUri(QStringLiteral("prof1"),
                                             QStringLiteral("/home/alice/notes.txt"));
    const remote::SshUri parsed = remote::parseSshUri(uri);

    ScintillaNext *e = ScintillaNext::createShell(QString(), &backend, uri, parsed.remotePath);
    QVERIFY(e != nullptr);

    // Identity is stamped BEFORE any bytes: File + remote + URI, in Loading state.
    QVERIFY(e->isFile());
    QVERIFY(e->isRemote());
    QCOMPARE(e->remoteUri(), uri);
    QCOMPARE(e->remotePath(), QStringLiteral("/home/alice/notes.txt"));
    QCOMPARE(e->getFilePath(), uri); // dedupe key
    QCOMPARE(e->loadState(), ScintillaNext::LoadState::Loading);
    // Tab title is the basename of the remote path.
    QCOMPARE(e->getName(), QStringLiteral("notes.txt"));
    // No undo/redo yet (undo collection is off during loading).
    QVERIFY(!e->canUndo());
    QVERIFY(!e->canRedo());

    delete e;
}

void TestAsyncEditorLoad::shellIsNotMistakenForScratchTab()
{
    FakeRemoteBackend backend;
    const QString uri = remote::formatSshUri(QStringLiteral("p"), QStringLiteral("/a/b.c"));
    ScintillaNext *e = ScintillaNext::createShell(QString(), &backend, uri,
                                                  remote::parseSshUri(uri).remotePath);

    // The whole point: a still-loading remote shell is isFile(), so the
    // initialEditor() reject test fires — it can never be reused/closed as a
    // pristine scratch tab mid-load.
    QVERIFY(wouldBeRejectedAsScratchTab(e));

    delete e;
}

void TestAsyncEditorLoad::loadIntoFillsBufferFromBackend()
{
    FakeRemoteBackend backend;
    const QString uri = remote::formatSshUri(QStringLiteral("p"), QStringLiteral("/x/hello.txt"));
    const QString path = remote::parseSshUri(uri).remotePath;
    backend.contents.insert(path, QByteArrayLiteral("hello remote world"));

    ScintillaNext *e = ScintillaNext::createShell(QString(), &backend, uri, path);

    QSignalSpy loadedSpy(e, &ScintillaNext::loaded);

    bool cbOk = false;
    e->loadInto([&cbOk](bool ok, const QString &) { cbOk = ok; });

    // Read is in flight — content has NOT arrived; the placeholder is shown.
    QCOMPARE(e->loadState(), ScintillaNext::LoadState::Loading);
    QCOMPARE(backend.pendingCount(), 1);

    backend.flush();

    QVERIFY(cbOk);
    QCOMPARE(loadedSpy.count(), 1);
    QCOMPARE(e->loadState(), ScintillaNext::LoadState::Loaded);
    const QByteArray text = e->get_text_range(0, static_cast<int>(e->textLength()));
    QCOMPARE(text, QByteArrayLiteral("hello remote world"));
    // Filled content is at the save point (clean), and editing is re-enabled.
    QVERIFY(!e->modify());
    QVERIFY(!e->readOnly());

    delete e;
}

void TestAsyncEditorLoad::loadIntoStripsUtf8Bom()
{
    FakeRemoteBackend backend;
    const QString uri = remote::formatSshUri(QStringLiteral("p"), QStringLiteral("/x/bom.txt"));
    const QString path = remote::parseSshUri(uri).remotePath;
    QByteArray withBom = QByteArray::fromHex("EFBBBF") + QByteArrayLiteral("data");
    backend.contents.insert(path, withBom);

    ScintillaNext *e = ScintillaNext::createShell(QString(), &backend, uri, path);
    e->loadInto();
    backend.flush();

    // UTF-8 BOM is detected + stripped from the document (mirrors readFromDisk).
    QCOMPARE(e->bom(), ScintillaNext::BomType::Utf8);
    const QByteArray text = e->get_text_range(0, static_cast<int>(e->textLength()));
    QCOMPARE(text, QByteArrayLiteral("data"));

    delete e;
}

void TestAsyncEditorLoad::secondOpenDoesNotDisturbLoadingTab()
{
    FakeRemoteBackend backend;
    const QString uriA = remote::formatSshUri(QStringLiteral("p"), QStringLiteral("/first.txt"));
    const QString uriB = remote::formatSshUri(QStringLiteral("p"), QStringLiteral("/second.txt"));
    const QString pathA = remote::parseSshUri(uriA).remotePath;
    const QString pathB = remote::parseSshUri(uriB).remotePath;
    backend.contents.insert(pathA, QByteArrayLiteral("AAAA"));
    backend.contents.insert(pathB, QByteArrayLiteral("BBBB"));

    ScintillaNext *first = ScintillaNext::createShell(QString(), &backend, uriA, pathA);
    first->loadInto();

    // First is still loading (callback not flushed).
    QCOMPARE(first->loadState(), ScintillaNext::LoadState::Loading);

    // Open a SECOND file while the first is in flight. The shell is created +
    // would be registered synchronously; the first tab is untouched — it is
    // still isFile() (so never reused as a scratch tab) and still Loading.
    ScintillaNext *second = ScintillaNext::createShell(QString(), &backend, uriB, pathB);
    second->loadInto();

    QVERIFY(first->isFile());
    QVERIFY(wouldBeRejectedAsScratchTab(first));
    QCOMPARE(first->loadState(), ScintillaNext::LoadState::Loading);

    // Now resolve both (FIFO). Each gets its own content.
    backend.flush();
    QCOMPARE(first->loadState(), ScintillaNext::LoadState::Loaded);
    QCOMPARE(second->loadState(), ScintillaNext::LoadState::Loaded);
    QCOMPARE(first->get_text_range(0, static_cast<int>(first->textLength())), QByteArrayLiteral("AAAA"));
    QCOMPARE(second->get_text_range(0, static_cast<int>(second->textLength())), QByteArrayLiteral("BBBB"));

    delete first;
    delete second;
}

void TestAsyncEditorLoad::readFailureEntersErrorStateThenRetrySucceeds()
{
    FakeRemoteBackend backend;
    const QString uri = remote::formatSshUri(QStringLiteral("p"), QStringLiteral("/denied.txt"));
    const QString path = remote::parseSshUri(uri).remotePath;

    ScintillaNext *e = ScintillaNext::createShell(QString(), &backend, uri, path);

    QSignalSpy failSpy(e, &ScintillaNext::loadFailed);

    // First attempt: forced failure → Error state + loadFailed.
    backend.nextReadFails = true;
    backend.nextReadError = QStringLiteral("permission denied");
    e->loadInto();
    backend.flush();

    QCOMPARE(e->loadState(), ScintillaNext::LoadState::Error);
    QCOMPARE(failSpy.count(), 1);

    // Retry: now the content is available → Loaded.
    backend.nextReadFails = false;
    backend.contents.insert(path, QByteArrayLiteral("recovered"));
    QSignalSpy loadedSpy(e, &ScintillaNext::loaded);
    e->retryLoad();
    QCOMPARE(e->loadState(), ScintillaNext::LoadState::Loading); // placeholder re-armed
    backend.flush();

    QCOMPARE(e->loadState(), ScintillaNext::LoadState::Loaded);
    QCOMPARE(loadedSpy.count(), 1);
    QCOMPARE(e->get_text_range(0, static_cast<int>(e->textLength())), QByteArrayLiteral("recovered"));

    delete e;
}

void TestAsyncEditorLoad::uriIsTheDedupeIdentity()
{
    FakeRemoteBackend backend;
    const QString uri = remote::formatSshUri(QStringLiteral("p"), QStringLiteral("/dup.txt"));
    const QString other = remote::formatSshUri(QStringLiteral("p"), QStringLiteral("/diff.txt"));
    const QString path = remote::parseSshUri(uri).remotePath;
    const QString otherPath = remote::parseSshUri(other).remotePath;

    ScintillaNext *a = ScintillaNext::createShell(QString(), &backend, uri, path);
    ScintillaNext *b = ScintillaNext::createShell(QString(), &backend, uri, path);
    ScintillaNext *c = ScintillaNext::createShell(QString(), &backend, other, otherPath);

    // Same URI → same dedupe key (EditorManager::getEditorByFilePath compares
    // remoteUri() for ssh:// paths). Different URI → different key.
    QCOMPARE(a->remoteUri(), b->remoteUri());
    QVERIFY(a->remoteUri() != c->remoteUri());
    QCOMPARE(a->getFilePath(), uri);
    QVERIFY(remote::isSshUri(a->getFilePath()));

    delete a;
    delete b;
    delete c;
}


QTEST_MAIN(TestAsyncEditorLoad)
#include "test_async_editor_load.moc"
