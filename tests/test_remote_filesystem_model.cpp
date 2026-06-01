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

// Offline coverage of RemoteFileSystemModel (D2/D11) — the single highest-risk
// piece of the remote-workspace work. Driven by a FAKE in-memory lister (a
// scriptable path → entries map), so NO network / SshConnection / libssh2 is
// pulled in: the model takes a ListFn seam exactly so it is unit-testable this
// way. Asserts the normative invariants:
//   - QAbstractItemModelTester conformance over a fully-settled tree
//   - index(path) / filePath / isDir round-trips across the three regions
//   - rowsInserted fires on fetchMore (lazy async listing materializes children)
//   - directoryLoaded fires per listed directory (the dock's restore contract)
//   - onDirectoryChanged diffs a re-listing into minimal rowsInserted/rowsRemoved
//   - empty directory settles (no children, hasChildren false, not loading)
//   - AND a proxy round-trip: wrapped in FolderAsWorkspaceProxyModel, the
//     synthetic-root mapToSource/mapFromSource/parent behave like the local proxy
//     test — proving the proxy's invariants hold verbatim over the remote model.

#include <QtTest>
#include <QAbstractItemModelTester>
#include <QDir>
#include <QSignalSpy>

#include "docks/FolderAsWorkspaceProxyModel.h"
#include "remote/RemoteFileSystemModel.h"

using namespace remote;

// A scriptable, in-memory directory lister. `dirs[path]` holds the entries a
// readdir of `path` returns. Resolution is SYNCHRONOUS by default (the callback
// fires inline) so the model settles deterministically without an event loop;
// flipping `deferred` queues callbacks so a test can observe the mid-fetch
// "loading" state and resolve it explicitly via flush().
class FakeLister
{
public:
    QHash<QString, QList<RemoteFileSystemModel::Entry>> dirs; // scripted listings per path
    QHash<QString, bool> failPaths;                 // paths that error on readdir
    QStringList requested;                          // every path asked for, in order
    bool deferred = false;

    // The ListFn the model is constructed with.
    RemoteFileSystemModel::ListFn fn()
    {
        return [this](const QString &path, RemoteFileSystemModel::ListDoneCallback done) {
            requested.append(path);
            if (deferred) {
                m_pending.append({path, std::move(done)});
                return;
            }
            resolve(path, std::move(done));
        };
    }

    // Resolve all queued (deferred) callbacks in arrival order.
    void flush()
    {
        auto pend = m_pending;
        m_pending.clear();
        for (auto &p : pend) {
            resolve(p.first, std::move(p.second));
        }
    }

    int pendingCount() const { return m_pending.size(); }

private:
    QList<QPair<QString, RemoteFileSystemModel::ListDoneCallback>> m_pending;

    void resolve(const QString &path, RemoteFileSystemModel::ListDoneCallback done)
    {
        if (!done) return;
        if (failPaths.value(path, false)) {
            done(false, {}, QStringLiteral("permission denied"));
            return;
        }
        done(true, dirs.value(path), QString());
    }
};

static RemoteFileSystemModel::Entry ent(const QString &name, bool isDir, qint64 size = 0)
{
    RemoteFileSystemModel::Entry e;
    e.name = name;
    e.isDir = isDir;
    e.size = size;
    e.mtimeSecs = 1700000000;
    return e;
}

class TestRemoteFilesystemModel : public QObject
{
    Q_OBJECT

private slots:
    void rootShowsAsSingleTopNode();
    void indexForPath_roundTrips();
    void fetchMore_emitsRowsInserted();
    void emptyDir_settlesNotLoading();
    void loadingRole_duringDeferredFetch();
    void directoryChanged_insertAndRemove();
    void modelTester_conformance();
    void proxy_roundTripAndSyntheticRoot();

private:
    // Standard scripted tree rooted at /ws:
    //   /ws            -> [a.txt, sub/, zzz.txt]
    //   /ws/sub        -> [b.txt, deep/]
    //   /ws/sub/deep   -> [c.txt]
    //   /ws/empty      -> []   (only referenced by the empty-dir test)
    static void buildTree(FakeLister &fake);
};

void TestRemoteFilesystemModel::buildTree(FakeLister &fake)
{
    fake.dirs[QStringLiteral("/ws")] = {
        ent(QStringLiteral("a.txt"), false, 1),
        ent(QStringLiteral("sub"), true),
        ent(QStringLiteral("zzz.txt"), false, 3),
    };
    fake.dirs[QStringLiteral("/ws/sub")] = {
        ent(QStringLiteral("b.txt"), false, 2),
        ent(QStringLiteral("deep"), true),
    };
    fake.dirs[QStringLiteral("/ws/sub/deep")] = {
        ent(QStringLiteral("c.txt"), false, 9),
    };
}

void TestRemoteFilesystemModel::rootShowsAsSingleTopNode()
{
    FakeLister fake;
    buildTree(fake);
    RemoteFileSystemModel model(fake.fn());

    model.setRootPath(QStringLiteral("/ws")); // synchronous fake → root listed now

    // Exactly one top-level row (the workspace dir), one column.
    QCOMPARE(model.rowCount(QModelIndex()), 1);
    QCOMPARE(model.columnCount(QModelIndex()), 1);

    const QModelIndex top = model.index(0, 0, QModelIndex());
    QVERIFY(top.isValid());
    QVERIFY(model.isDir(top));
    QCOMPARE(model.filePath(top), QStringLiteral("/ws"));
    QVERIFY(model.hasChildren(top));
    QCOMPARE(model.rowCount(top), 3); // a.txt, sub/, zzz.txt
    // Root's own parent is the invisible top.
    QCOMPARE(model.parent(top), QModelIndex());
}

void TestRemoteFilesystemModel::indexForPath_roundTrips()
{
    FakeLister fake;
    buildTree(fake);
    RemoteFileSystemModel model(fake.fn());
    model.setRootPath(QStringLiteral("/ws"));

    // Drill into sub/ and sub/deep/ so the deep path is resolvable.
    model.fetchMore(model.indexForPath(QStringLiteral("/ws/sub")));
    model.fetchMore(model.indexForPath(QStringLiteral("/ws/sub/deep")));

    // Root.
    const QModelIndex root = model.indexForPath(QStringLiteral("/ws"));
    QVERIFY(root.isValid());
    QCOMPARE(model.filePath(root), QStringLiteral("/ws"));
    QVERIFY(model.isDir(root));

    // Direct child file.
    const QModelIndex a = model.indexForPath(QStringLiteral("/ws/a.txt"));
    QVERIFY(a.isValid());
    QCOMPARE(model.filePath(a), QStringLiteral("/ws/a.txt"));
    QVERIFY(!model.isDir(a));
    QCOMPARE(model.parent(a), root);

    // Mid-level dir.
    const QModelIndex sub = model.indexForPath(QStringLiteral("/ws/sub"));
    QVERIFY(sub.isValid());
    QVERIFY(model.isDir(sub));
    QCOMPARE(model.parent(sub), root);

    // Deep leaf — full passthrough far below the root.
    const QModelIndex c = model.indexForPath(QStringLiteral("/ws/sub/deep/c.txt"));
    QVERIFY(c.isValid());
    QCOMPARE(model.filePath(c), QStringLiteral("/ws/sub/deep/c.txt"));
    QVERIFY(!model.isDir(c));
    QCOMPARE(model.parent(c), model.indexForPath(QStringLiteral("/ws/sub/deep")));

    // A not-yet-listed path resolves to an invalid index (the dock waits on
    // directoryLoaded), and a path outside the root never resolves.
    QVERIFY(!model.indexForPath(QStringLiteral("/elsewhere/x")).isValid());
}

void TestRemoteFilesystemModel::fetchMore_emitsRowsInserted()
{
    FakeLister fake;
    buildTree(fake);
    RemoteFileSystemModel model(fake.fn());
    model.setRootPath(QStringLiteral("/ws"));

    const QModelIndex sub = model.indexForPath(QStringLiteral("/ws/sub"));
    QVERIFY(sub.isValid());
    QCOMPARE(model.rowCount(sub), 0);     // not listed yet
    QVERIFY(model.hasChildren(sub));      // but reports children (expand affordance)
    QVERIFY(model.canFetchMore(sub));

    QSignalSpy insertSpy(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy loadedSpy(&model, &RemoteFileSystemModel::directoryLoaded);
    QVERIFY(insertSpy.isValid());
    QVERIFY(loadedSpy.isValid());

    model.fetchMore(sub);

    QCOMPARE(model.rowCount(sub), 2);     // b.txt, deep/
    QVERIFY(insertSpy.count() >= 1);
    QVERIFY(!model.canFetchMore(sub));    // listed → no re-fetch
    // directoryLoaded fired for sub (the dock's restore/expansion contract).
    bool sawSub = false;
    for (const QList<QVariant> &args : loadedSpy) {
        if (args.at(0).toString() == QStringLiteral("/ws/sub")) sawSub = true;
    }
    QVERIFY(sawSub);
}

void TestRemoteFilesystemModel::emptyDir_settlesNotLoading()
{
    FakeLister fake;
    fake.dirs[QStringLiteral("/empty")] = {}; // scripted, present, no entries
    RemoteFileSystemModel model(fake.fn());
    model.setRootPath(QStringLiteral("/empty"));

    const QModelIndex root = model.index(0, 0, QModelIndex());
    QVERIFY(root.isValid());
    QCOMPARE(model.rowCount(root), 0);
    QVERIFY(!model.hasChildren(root));               // settled empty, not spinning
    QVERIFY(!model.canFetchMore(root));              // already listed
    QCOMPARE(model.data(root, RemoteFileSystemModel::LoadingRole).toBool(), false);
}

void TestRemoteFilesystemModel::loadingRole_duringDeferredFetch()
{
    FakeLister fake;
    buildTree(fake);
    fake.deferred = true;                            // hold the readdir callback
    RemoteFileSystemModel model(fake.fn());

    QSignalSpy loadedSpy(&model, &RemoteFileSystemModel::directoryLoaded);
    model.setRootPath(QStringLiteral("/ws"));        // posts the fetch, returns now

    const QModelIndex root = model.index(0, 0, QModelIndex());
    QVERIFY(root.isValid());
    QCOMPARE(model.rowCount(root), 0);               // children not arrived
    QCOMPARE(model.data(root, RemoteFileSystemModel::LoadingRole).toBool(), true);
    QCOMPARE(loadedSpy.count(), 0);

    fake.flush();                                    // deliver the listing

    QCOMPARE(model.data(root, RemoteFileSystemModel::LoadingRole).toBool(), false);
    QCOMPARE(model.rowCount(root), 3);
    QVERIFY(loadedSpy.count() >= 1);
}

void TestRemoteFilesystemModel::directoryChanged_insertAndRemove()
{
    FakeLister fake;
    buildTree(fake);
    RemoteFileSystemModel model(fake.fn());
    model.setRootPath(QStringLiteral("/ws"));
    QCOMPARE(model.rowCount(model.indexForPath(QStringLiteral("/ws"))), 3);

    // Simulate an external change: zzz.txt removed, new.txt created.
    fake.dirs[QStringLiteral("/ws")] = {
        ent(QStringLiteral("a.txt"), false, 1),
        ent(QStringLiteral("sub"), true),
        ent(QStringLiteral("new.txt"), false, 5),
    };

    QSignalSpy insertSpy(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removeSpy(&model, &QAbstractItemModel::rowsRemoved);
    QVERIFY(insertSpy.isValid());
    QVERIFY(removeSpy.isValid());

    model.onDirectoryChanged(QStringLiteral("/ws")); // re-list + diff (synchronous)

    const QModelIndex root = model.indexForPath(QStringLiteral("/ws"));
    QCOMPARE(model.rowCount(root), 3);                       // a.txt, sub/, new.txt
    QVERIFY(removeSpy.count() >= 1);                          // zzz.txt removed
    QVERIFY(insertSpy.count() >= 1);                          // new.txt inserted
    QVERIFY(model.indexForPath(QStringLiteral("/ws/new.txt")).isValid());
    QVERIFY(!model.indexForPath(QStringLiteral("/ws/zzz.txt")).isValid());
}

void TestRemoteFilesystemModel::modelTester_conformance()
{
    FakeLister fake;
    buildTree(fake);
    RemoteFileSystemModel model(fake.fn());
    model.setRootPath(QStringLiteral("/ws"));

    // Pre-list every directory so the tree is fully settled before the tester
    // attaches — once listed, canFetchMore is false everywhere, so the tester's
    // traversal never triggers a fresh fetch that would mutate row counts
    // mid-check (mirrors the local proxy test's settle-first strategy).
    model.fetchMore(model.indexForPath(QStringLiteral("/ws/sub")));
    model.fetchMore(model.indexForPath(QStringLiteral("/ws/sub/deep")));

    // Default (QtTest) reporting mode FAILS the test on any conformance violation.
    QAbstractItemModelTester tester(&model);

    const QModelIndex root = model.indexForPath(QStringLiteral("/ws"));
    QVERIFY(root.isValid());
    QCOMPARE(model.rowCount(root), 3);
    QCOMPARE(model.rowCount(model.indexForPath(QStringLiteral("/ws/sub"))), 2);
}

void TestRemoteFilesystemModel::proxy_roundTripAndSyntheticRoot()
{
    FakeLister fake;
    buildTree(fake);
    RemoteFileSystemModel model(fake.fn());
    FolderAsWorkspaceProxyModel proxy;
    proxy.setSourceModel(&model);
    model.setRootPath(QStringLiteral("/ws"));
    proxy.setRootSourcePath(QStringLiteral("/ws"));

    // Settle the subtree the round-trip touches.
    model.fetchMore(model.indexForPath(QStringLiteral("/ws/sub")));
    model.fetchMore(model.indexForPath(QStringLiteral("/ws/sub/deep")));

    // The proxy must pass the model-tester over the remote model too.
    QAbstractItemModelTester tester(&proxy);

    // One synthetic top row that maps to the workspace root.
    QCOMPARE(proxy.rowCount(QModelIndex()), 1);
    QCOMPARE(proxy.columnCount(QModelIndex()), 1);

    // Region 1: synthetic root.
    const QModelIndex rootSrc = model.indexForPath(QStringLiteral("/ws"));
    QVERIFY(rootSrc.isValid());
    const QModelIndex pr = proxy.mapFromSource(rootSrc);
    QVERIFY(pr.isValid());
    QCOMPARE(proxy.mapToSource(pr), rootSrc);
    QCOMPARE(proxy.mapFromSource(proxy.mapToSource(pr)), pr);
    QCOMPARE(proxy.parent(pr), QModelIndex());            // closes the tree at the top
    QCOMPARE(QDir::cleanPath(proxy.filePath(pr)), QStringLiteral("/ws"));
    QVERIFY(proxy.isDir(pr));

    // Region 2: a direct child of the root (sub/).
    const QModelIndex subSrc = model.indexForPath(QStringLiteral("/ws/sub"));
    QVERIFY(subSrc.isValid());
    const QModelIndex subProxy = proxy.mapFromSource(subSrc);
    QVERIFY(subProxy.isValid());
    QCOMPARE(proxy.mapToSource(subProxy), subSrc);
    QCOMPARE(proxy.parent(subProxy), pr);

    // Region 3: deep passthrough node (sub/deep/c.txt).
    const QModelIndex leafSrc = model.indexForPath(QStringLiteral("/ws/sub/deep/c.txt"));
    QVERIFY(leafSrc.isValid());
    const QModelIndex leafProxy = proxy.mapFromSource(leafSrc);
    QVERIFY(leafProxy.isValid());
    QCOMPARE(proxy.mapToSource(leafProxy), leafSrc);
    QCOMPARE(proxy.mapFromSource(proxy.mapToSource(leafProxy)), leafProxy);
    QCOMPARE(QDir::cleanPath(proxy.filePath(leafProxy)),
             QStringLiteral("/ws/sub/deep/c.txt"));
}

QTEST_MAIN(TestRemoteFilesystemModel)
#include "test_remote_filesystem_model.moc"
