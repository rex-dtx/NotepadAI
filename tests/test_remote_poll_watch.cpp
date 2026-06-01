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

// Offline coverage of RemoteDirectoryWatcher (D3 / task 3.1+3.2) — the bounded
// remote-workspace poll-watcher. It depends ONLY on two injected std::function
// seams (a PollFn standing in for RemoteFsBackend::readdirAsync, and a
// VisibleDirsFn standing in for the dock's expanded-dir walk), so NO network /
// SshConnection / RemoteFsBackend / libssh2 is linked. Each test scripts a
// path → entries map and drives poll-ticks directly via pollNowForTest(),
// asserting the normative invariants:
//   - first sighting of a dir seeds the cache SILENTLY (no spurious change)
//   - a subsequent ADD is detected → directoryChanged(path)
//   - a subsequent REMOVE is detected → directoryChanged(path)
//   - an unchanged listing emits NOTHING
//   - ONLY visible/expanded dirs are polled — a deep tree with one expanded dir
//     polls exactly that dir (non-recursive); collapsing prunes it
//   - interval gating: 2s focused+visible, 10s unfocused, paused (timer off /
//     interval 0) when minimized OR the dock is hidden
//   - a transient readdir failure does NOT clobber the cached baseline

#include <QtTest>
#include <QSignalSpy>

#include "remote/RemoteDirectoryWatcher.h"

using namespace remote;

// A scriptable, in-memory directory lister mirroring FakeSshTransport's role for
// the SFTP engine. `dirs[path]` holds the entries a readdir of `path` returns.
// Resolution is SYNCHRONOUS (callback fires inline) so the watcher settles
// deterministically without an event loop; `failPaths` forces an ok=false reply.
class FakePoller
{
public:
    QHash<QString, QList<RemoteDirectoryWatcher::DirEntry>> dirs; // scripted listings
    QHash<QString, bool> failPaths;                               // readdir errors
    QStringList requested;                                        // every path polled, in order

    RemoteDirectoryWatcher::PollFn fn()
    {
        return [this](const QString &path, RemoteDirectoryWatcher::PollDoneCallback done) {
            requested.append(path);
            if (!done) return;
            if (failPaths.value(path, false)) {
                done(false, {});
                return;
            }
            done(true, dirs.value(path));
        };
    }
};

static RemoteDirectoryWatcher::DirEntry de(const QString &name, bool isDir, qint64 size = 0,
                                           qint64 mtime = 1700000000)
{
    RemoteDirectoryWatcher::DirEntry e;
    e.name = name;
    e.isDir = isDir;
    e.size = size;
    e.mtimeSecs = mtime;
    return e;
}

class TestRemotePollWatch : public QObject
{
    Q_OBJECT

private slots:
    void firstPollSeedsSilently();
    void detectsAdd();
    void detectsRemove();
    void detectsMetadataChange();
    void unchangedEmitsNothing();
    void onlyVisibleDirsPolled_nonRecursive();
    void collapsePrunesCache();
    void transientFailureKeepsBaseline();
    void intervalGating_focusVisibleMinimize();
    void intervalGating_dockHiddenPauses();
    void inFlightGuardAvoidsPileup();
};

void TestRemotePollWatch::firstPollSeedsSilently()
{
    FakePoller poller;
    poller.dirs[QStringLiteral("/ws")] = {de("a.txt", false, 1), de("b.txt", false, 2)};

    RemoteDirectoryWatcher watcher(poller.fn(), []() {
        return QStringList{QStringLiteral("/ws")};
    });
    QSignalSpy changedSpy(&watcher, &RemoteDirectoryWatcher::directoryChanged);

    watcher.pollNowForTest();

    // The model just listed /ws; the watcher's first observation is the baseline,
    // NOT a change — no signal, cache seeded.
    QCOMPARE(changedSpy.count(), 0);
    QCOMPARE(watcher.cachedEntryCountForTest(QStringLiteral("/ws")), 2);
    QCOMPARE(poller.requested, QStringList{QStringLiteral("/ws")});
}

void TestRemotePollWatch::detectsAdd()
{
    FakePoller poller;
    poller.dirs[QStringLiteral("/ws")] = {de("a.txt", false, 1)};

    RemoteDirectoryWatcher watcher(poller.fn(), []() {
        return QStringList{QStringLiteral("/ws")};
    });
    QSignalSpy changedSpy(&watcher, &RemoteDirectoryWatcher::directoryChanged);

    watcher.pollNowForTest(); // seed baseline {a.txt}
    QCOMPARE(changedSpy.count(), 0);

    // Another process creates new.txt.
    poller.dirs[QStringLiteral("/ws")] = {de("a.txt", false, 1), de("new.txt", false, 5)};
    watcher.pollNowForTest();

    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(changedSpy.at(0).at(0).toString(), QStringLiteral("/ws"));
    QCOMPARE(watcher.cachedEntryCountForTest(QStringLiteral("/ws")), 2);
}

void TestRemotePollWatch::detectsRemove()
{
    FakePoller poller;
    poller.dirs[QStringLiteral("/ws")] = {de("a.txt", false, 1), de("gone.txt", false, 3)};

    RemoteDirectoryWatcher watcher(poller.fn(), []() {
        return QStringList{QStringLiteral("/ws")};
    });
    QSignalSpy changedSpy(&watcher, &RemoteDirectoryWatcher::directoryChanged);

    watcher.pollNowForTest(); // seed baseline {a.txt, gone.txt}
    QCOMPARE(changedSpy.count(), 0);

    poller.dirs[QStringLiteral("/ws")] = {de("a.txt", false, 1)}; // gone.txt deleted
    watcher.pollNowForTest();

    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(changedSpy.at(0).at(0).toString(), QStringLiteral("/ws"));
    QCOMPARE(watcher.cachedEntryCountForTest(QStringLiteral("/ws")), 1);
}

void TestRemotePollWatch::detectsMetadataChange()
{
    FakePoller poller;
    poller.dirs[QStringLiteral("/ws")] = {de("a.txt", false, /*size*/ 1, /*mtime*/ 100)};

    RemoteDirectoryWatcher watcher(poller.fn(), []() {
        return QStringList{QStringLiteral("/ws")};
    });
    QSignalSpy changedSpy(&watcher, &RemoteDirectoryWatcher::directoryChanged);

    watcher.pollNowForTest(); // baseline
    QCOMPARE(changedSpy.count(), 0);

    // Same name, but the file was edited (size + mtime bumped).
    poller.dirs[QStringLiteral("/ws")] = {de("a.txt", false, /*size*/ 42, /*mtime*/ 200)};
    watcher.pollNowForTest();

    QCOMPARE(changedSpy.count(), 1);
}

void TestRemotePollWatch::unchangedEmitsNothing()
{
    FakePoller poller;
    poller.dirs[QStringLiteral("/ws")] = {de("a.txt", false, 1), de("sub", true)};

    RemoteDirectoryWatcher watcher(poller.fn(), []() {
        return QStringList{QStringLiteral("/ws")};
    });
    QSignalSpy changedSpy(&watcher, &RemoteDirectoryWatcher::directoryChanged);

    watcher.pollNowForTest(); // seed
    watcher.pollNowForTest(); // identical listing
    watcher.pollNowForTest(); // identical listing

    QCOMPARE(changedSpy.count(), 0);
}

void TestRemotePollWatch::onlyVisibleDirsPolled_nonRecursive()
{
    // A deep tree, but only the root is "expanded": the watcher must poll ONLY
    // the root, never descend into sub/ or sub/deep/.
    FakePoller poller;
    poller.dirs[QStringLiteral("/ws")] = {de("sub", true)};
    poller.dirs[QStringLiteral("/ws/sub")] = {de("deep", true)};
    poller.dirs[QStringLiteral("/ws/sub/deep")] = {de("c.txt", false, 9)};

    // VisibleDirsFn returns only the root — exactly what the dock yields when the
    // user has expanded nothing below it.
    RemoteDirectoryWatcher watcher(poller.fn(), []() {
        return QStringList{QStringLiteral("/ws")};
    });

    watcher.pollNowForTest();
    watcher.pollNowForTest();

    // /ws was polled (twice); the descendants were NEVER polled.
    QVERIFY(poller.requested.contains(QStringLiteral("/ws")));
    QVERIFY(!poller.requested.contains(QStringLiteral("/ws/sub")));
    QVERIFY(!poller.requested.contains(QStringLiteral("/ws/sub/deep")));
    QCOMPARE(watcher.cachedEntryCountForTest(QStringLiteral("/ws/sub")), 0);
}

void TestRemotePollWatch::collapsePrunesCache()
{
    FakePoller poller;
    poller.dirs[QStringLiteral("/ws")] = {de("sub", true)};
    poller.dirs[QStringLiteral("/ws/sub")] = {de("b.txt", false, 2)};

    // Mutable visible set: start with both /ws and /ws/sub expanded, then collapse
    // sub (drop it from the set) and verify its cache is pruned.
    QStringList visible{QStringLiteral("/ws"), QStringLiteral("/ws/sub")};
    RemoteDirectoryWatcher watcher(poller.fn(), [&visible]() { return visible; });

    watcher.pollNowForTest(); // seed both
    QCOMPARE(watcher.cachedEntryCountForTest(QStringLiteral("/ws/sub")), 1);

    visible = QStringList{QStringLiteral("/ws")}; // user collapsed sub
    watcher.pollNowForTest();

    // sub is no longer visible → pruned from the cache, and not re-polled.
    QCOMPARE(watcher.cachedEntryCountForTest(QStringLiteral("/ws/sub")), 0);
    const int subPolls = poller.requested.count(QStringLiteral("/ws/sub"));
    QCOMPARE(subPolls, 1); // polled once (the seed), never after collapse
}

void TestRemotePollWatch::transientFailureKeepsBaseline()
{
    FakePoller poller;
    poller.dirs[QStringLiteral("/ws")] = {de("a.txt", false, 1), de("b.txt", false, 2)};

    RemoteDirectoryWatcher watcher(poller.fn(), []() {
        return QStringList{QStringLiteral("/ws")};
    });
    QSignalSpy changedSpy(&watcher, &RemoteDirectoryWatcher::directoryChanged);

    watcher.pollNowForTest(); // baseline {a.txt, b.txt}
    QCOMPARE(watcher.cachedEntryCountForTest(QStringLiteral("/ws")), 2);

    // A blip: readdir fails. The cached baseline must survive (a failure must
    // NOT read as "everything deleted"), and no change is emitted.
    poller.failPaths[QStringLiteral("/ws")] = true;
    watcher.pollNowForTest();
    QCOMPARE(changedSpy.count(), 0);
    QCOMPARE(watcher.cachedEntryCountForTest(QStringLiteral("/ws")), 2);

    // Recovery: same listing comes back → still no spurious change.
    poller.failPaths[QStringLiteral("/ws")] = false;
    watcher.pollNowForTest();
    QCOMPARE(changedSpy.count(), 0);
}

void TestRemotePollWatch::intervalGating_focusVisibleMinimize()
{
    FakePoller poller;
    RemoteDirectoryWatcher watcher(poller.fn(), []() { return QStringList(); });

    // Before start(): inert regardless of state.
    QVERIFY(!watcher.isPollingActiveForTest());

    // Default state is focused + dock-visible + not-minimized → 2s once armed.
    watcher.start();
    QVERIFY(watcher.isPollingActiveForTest());
    QCOMPARE(watcher.currentIntervalMsForTest(), RemoteDirectoryWatcher::kFocusedIntervalMs);

    // Lose focus → 10s, still active.
    watcher.setWindowFocused(false);
    QVERIFY(watcher.isPollingActiveForTest());
    QCOMPARE(watcher.currentIntervalMsForTest(), RemoteDirectoryWatcher::kUnfocusedIntervalMs);

    // Minimize → paused (interval 0, timer stopped) regardless of focus.
    watcher.setWindowMinimized(true);
    QCOMPARE(watcher.currentIntervalMsForTest(), 0);
    QVERIFY(!watcher.isPollingActiveForTest());

    // Restore + regain focus → back to 2s active.
    watcher.setWindowMinimized(false);
    watcher.setWindowFocused(true);
    QVERIFY(watcher.isPollingActiveForTest());
    QCOMPARE(watcher.currentIntervalMsForTest(), RemoteDirectoryWatcher::kFocusedIntervalMs);

    // stop() disarms entirely.
    watcher.stop();
    QVERIFY(!watcher.isPollingActiveForTest());
}

void TestRemotePollWatch::intervalGating_dockHiddenPauses()
{
    FakePoller poller;
    RemoteDirectoryWatcher watcher(poller.fn(), []() { return QStringList(); });
    watcher.start();
    QVERIFY(watcher.isPollingActiveForTest());

    // Hiding the dock pauses polling even while focused + not-minimized.
    watcher.setDockVisible(false);
    QCOMPARE(watcher.currentIntervalMsForTest(), 0);
    QVERIFY(!watcher.isPollingActiveForTest());

    // Re-showing resumes.
    watcher.setDockVisible(true);
    QVERIFY(watcher.isPollingActiveForTest());
    QCOMPARE(watcher.currentIntervalMsForTest(), RemoteDirectoryWatcher::kFocusedIntervalMs);
}

void TestRemotePollWatch::inFlightGuardAvoidsPileup()
{
    // A DEFERRED poller: readdir callbacks are queued, not fired inline, so a
    // second poll tick lands while the first is still "in flight". The watcher
    // must skip the dir whose poll hasn't returned — no request pile-up.
    struct DeferredPoller
    {
        QList<QPair<QString, RemoteDirectoryWatcher::PollDoneCallback>> pending;
        QStringList requested;
        QHash<QString, QList<RemoteDirectoryWatcher::DirEntry>> dirs;

        RemoteDirectoryWatcher::PollFn fn()
        {
            return [this](const QString &path, RemoteDirectoryWatcher::PollDoneCallback done) {
                requested.append(path);
                pending.append({path, std::move(done)});
            };
        }
        void flush()
        {
            auto p = pending;
            pending.clear();
            for (auto &x : p) {
                if (x.second) x.second(true, dirs.value(x.first));
            }
        }
    } poller;
    poller.dirs[QStringLiteral("/ws")] = {de("a.txt", false, 1)};

    RemoteDirectoryWatcher watcher(poller.fn(), []() {
        return QStringList{QStringLiteral("/ws")};
    });

    watcher.pollNowForTest(); // issues a readdir for /ws (deferred)
    watcher.pollNowForTest(); // /ws still in flight → must NOT issue a second
    QCOMPARE(poller.requested.count(QStringLiteral("/ws")), 1);

    poller.flush();           // first poll resolves → /ws no longer in flight
    watcher.pollNowForTest(); // now a fresh readdir is allowed
    QCOMPARE(poller.requested.count(QStringLiteral("/ws")), 2);
}

QTEST_MAIN(TestRemotePollWatch)
#include "test_remote_poll_watch.moc"
