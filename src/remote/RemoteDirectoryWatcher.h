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

#ifndef REMOTE_REMOTEDIRECTORYWATCHER_H
#define REMOTE_REMOTEDIRECTORYWATCHER_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>

class QTimer;

namespace remote {

// Bounded poll-watcher for a remote (SFTP) workspace file tree (D3).
//
// Remote filesystems have no portable change-notification primitive (inotify is
// Linux-only and depends on a fragile remote binary), so this polls SFTP
// `readdir` on ONLY the directories currently expanded/visible in the tree —
// never recursively over the whole subtree. A deep tree with only the root
// expanded polls exactly ONE directory.
//
// Adaptive interval (event-driven QTimer, no busy-loop):
//   - 2s  when the window is focused AND the dock is visible (the active case),
//   - 10s when the window is unfocused but the dock still visible (background),
//   - PAUSED (timer stopped) when the window is minimized OR the dock is hidden.
// The owner (the dock) feeds focus/visibility/minimize state via the setters;
// each setter recomputes the schedule. start()/stop() gate the whole thing.
//
// Channel reuse: the watcher does NOT own a connection or channel. It calls an
// injected PollFn (the dock adapts RemoteFsBackend::readdirAsync) which posts the
// readdir onto the connection's worker thread — the SAME single reused SFTP
// session every other op uses. The worker serializes SFTP, so per-interval polls
// just queue behind interactive ops; no new channel is ever opened.
//
// Change detection: each poll diffs the fresh listing of a visible dir against a
// cached listing for that dir (name → {isDir,size,mtime}); on the FIRST sighting
// of a dir the cache is seeded SILENTLY (the model just listed it — that is the
// baseline, not a change), and on any subsequent divergence (add / remove /
// metadata change) the cache is replaced and `directoryChanged(path)` is emitted.
// The consumer (RemoteFileSystemModel::onDirectoryChanged) authoritatively
// re-lists + diffs that one dir into minimal rowsInserted/rowsRemoved — so the
// watcher only needs to detect THAT a visible dir changed, not compute the row
// delta itself. The model stays the single source of truth for row state and the
// remote change-notification path is identical in shape to the local one
// (QFileSystemModel's own watcher → rowsInserted/Removed through the proxy).
//
// Complexity: O(V) readdir round-trips per interval where V = visible/expanded
// dir count (typically < 20); each diff is O(entries) over a hashed snapshot. No
// recursion, no per-file stat. Unchanged polls cost one readdir per visible dir
// and zero model work.
//
// Threading: lives on the UI thread; touches no libssh2. PollFn posts queued
// requests and its callback fires on the UI thread (RemoteFsBackend's contract).
//
// Testability: the watcher depends only on the two injected std::function seams
// (PollFn + VisibleDirsFn) — NO SshConnection / RemoteFsBackend / libssh2 — so a
// unit test scripts a path → entries map and drives poll-ticks directly via
// pollNowForTest(); interval gating is asserted via currentIntervalMsForTest().
class RemoteDirectoryWatcher : public QObject
{
    Q_OBJECT

public:
    // One observed directory entry. A self-contained POD (no SSH dependency) so
    // the watcher is link-free of libssh2 and unit-testable with a plain lister.
    // Equality covers every field the tree cares about, so a rename/resize/mtime
    // bump on any entry is detected as a change.
    struct DirEntry
    {
        QString name;       // basename, no path
        bool    isDir = false;
        qint64  size = 0;
        qint64  mtimeSecs = 0;

        bool operator==(const DirEntry &o) const
        {
            return name == o.name && isDir == o.isDir && size == o.size
                   && mtimeSecs == o.mtimeSecs;
        }
        bool operator!=(const DirEntry &o) const { return !(*this == o); }
    };

    // Async readdir of one absolute remote path. `done` is invoked exactly once on
    // the UI thread; `ok=false` leaves the cache for that dir untouched (a
    // transient error must not look like "everything was deleted"). The dock
    // adapts RemoteFsBackend::readdirAsync to this.
    using PollDoneCallback = std::function<void(bool ok, const QList<DirEntry> &entries)>;
    using PollFn = std::function<void(const QString &path, PollDoneCallback done)>;

    // Snapshot of the directories currently expanded/visible in the tree, as
    // cleaned absolute paths. Called once per poll tick; the dock walks the
    // (proxy) tree for expanded dir rows. NON-recursive by construction: only
    // expanded dirs are returned, so a collapsed subtree contributes nothing.
    using VisibleDirsFn = std::function<QStringList()>;

    // Default intervals (ms). Public so the owner/tests can reference them.
    static constexpr int kFocusedIntervalMs = 2000;   // focused + dock visible
    static constexpr int kUnfocusedIntervalMs = 10000; // unfocused, dock visible

    RemoteDirectoryWatcher(PollFn poll, VisibleDirsFn visibleDirs,
                           QObject *parent = nullptr);
    ~RemoteDirectoryWatcher() override;

    // Arm the watcher: from here on it polls on the schedule implied by the
    // current focus/visibility/minimize state. Idempotent. Before start() the
    // watcher is inert (no timer) regardless of state, so the owner can set the
    // initial state then arm once the model is rooted.
    void start();
    // Disarm: stop the timer and drop all in-flight poll bookkeeping. Idempotent.
    // Called from the owner's destructor BEFORE the tree/proxy are torn down so a
    // late callback can never touch a half-destroyed dock.
    void stop();

    // UI-state inputs. Each recomputes the schedule (start/stop the timer, adjust
    // the interval) immediately. Cheap and idempotent — safe to call on every
    // focus/visibility transition.
    void setWindowFocused(bool focused);
    void setDockVisible(bool visible);
    void setWindowMinimized(bool minimized);

    // --- test hooks ----------------------------------------------------------
    // Run exactly one poll sweep now, independent of the timer (drives the same
    // code path the timeout would). Lets a test step add/remove detection
    // deterministically without spinning an event loop for 2s.
    void pollNowForTest() { poll(); }
    // The interval the current state implies, or 0 when paused (minimized/hidden).
    int currentIntervalMsForTest() const { return desiredIntervalMs(); }
    // Whether the poll timer is currently running (armed AND not paused).
    bool isPollingActiveForTest() const;
    // Cached entry count for a dir (0 if never polled) — lets a test assert the
    // baseline-seed / prune behavior.
    int cachedEntryCountForTest(const QString &path) const;

signals:
    // A visible directory's listing diverged from its cached baseline (add /
    // remove / metadata change). The consumer re-lists + diffs `path` into row
    // changes. Same seam the local QFileSystemWatcher path funnels through.
    void directoryChanged(const QString &path);

private:
    // 0 when paused (minimized or dock hidden), else the focused/unfocused rate.
    int desiredIntervalMs() const;
    // Recompute timer state from m_running + the desired interval. Stops the
    // timer when paused/inert; (re)starts it with the right interval otherwise.
    void updateSchedule();
    // One sweep: snapshot visible dirs, prune cache for now-hidden dirs, issue a
    // readdir per visible dir (skipping ones with a poll already in flight).
    void poll();
    // Resolve a finished readdir for `path`: diff vs cache, seed-or-emit.
    void onPolled(const QString &path, bool ok, const QList<DirEntry> &entries);

    PollFn        m_poll;
    VisibleDirsFn m_visibleDirs;
    QTimer       *m_timer = nullptr;

    bool m_running = false;     // start()/stop() gate
    bool m_focused = true;      // window has focus
    bool m_dockVisible = true;  // the workspace dock is visible
    bool m_minimized = false;   // the window is minimized

    // Cached listing per visible dir: path → (name → entry). Pruned to the
    // visible set on every poll so it never grows past the visible-dir count.
    QHash<QString, QHash<QString, DirEntry>> m_cache;
    // Dirs with a readdir currently in flight — guards against pile-up when a
    // poll interval elapses before the prior round's callbacks land (slow link).
    QSet<QString> m_inFlight;
};

} // namespace remote

#endif // REMOTE_REMOTEDIRECTORYWATCHER_H
