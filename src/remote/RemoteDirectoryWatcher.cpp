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

#include "RemoteDirectoryWatcher.h"

#include <QPointer>
#include <QTimer>

#include <utility>

namespace remote {

RemoteDirectoryWatcher::RemoteDirectoryWatcher(PollFn poll, VisibleDirsFn visibleDirs,
                                               QObject *parent)
    : QObject(parent)
    , m_poll(std::move(poll))
    , m_visibleDirs(std::move(visibleDirs))
    , m_timer(new QTimer(this))
{
    // Single-shot=false: a periodic poll. setInterval is driven by updateSchedule
    // from the focus/visibility state; the timer is not started until start().
    m_timer->setTimerType(Qt::CoarseTimer); // 2s/10s cadence — coarse is cheapest
    connect(m_timer, &QTimer::timeout, this, &RemoteDirectoryWatcher::poll);
}

RemoteDirectoryWatcher::~RemoteDirectoryWatcher() = default;

void RemoteDirectoryWatcher::start()
{
    if (m_running) {
        return;
    }
    m_running = true;
    updateSchedule();
}

void RemoteDirectoryWatcher::stop()
{
    m_running = false;
    m_timer->stop();
    m_inFlight.clear();
}

void RemoteDirectoryWatcher::setWindowFocused(bool focused)
{
    if (m_focused == focused) {
        return;
    }
    m_focused = focused;
    updateSchedule();
}

void RemoteDirectoryWatcher::setDockVisible(bool visible)
{
    if (m_dockVisible == visible) {
        return;
    }
    m_dockVisible = visible;
    updateSchedule();
}

void RemoteDirectoryWatcher::setWindowMinimized(bool minimized)
{
    if (m_minimized == minimized) {
        return;
    }
    m_minimized = minimized;
    updateSchedule();
}

int RemoteDirectoryWatcher::desiredIntervalMs() const
{
    // Paused when the user can't see the tree at all.
    if (m_minimized || !m_dockVisible) {
        return 0;
    }
    return m_focused ? kFocusedIntervalMs : kUnfocusedIntervalMs;
}

void RemoteDirectoryWatcher::updateSchedule()
{
    const int interval = desiredIntervalMs();
    if (!m_running || interval == 0) {
        // Inert or paused: stop the timer. Don't touch the cache — when polling
        // resumes the cached baselines are still valid (the first post-resume
        // poll detects anything that changed while paused).
        if (m_timer->isActive()) {
            m_timer->stop();
        }
        return;
    }
    // Armed + active. (Re)start only when the interval actually changed so a
    // focus flip doesn't reset the countdown every time.
    if (!m_timer->isActive() || m_timer->interval() != interval) {
        m_timer->start(interval);
    }
}

bool RemoteDirectoryWatcher::isPollingActiveForTest() const
{
    return m_timer->isActive();
}

int RemoteDirectoryWatcher::cachedEntryCountForTest(const QString &path) const
{
    return static_cast<int>(m_cache.value(path).size());
}

void RemoteDirectoryWatcher::poll()
{
    if (!m_visibleDirs || !m_poll) {
        return;
    }

    const QStringList visible = m_visibleDirs();

    // Prune cache + in-flight bookkeeping for dirs no longer visible (collapsed
    // or removed from the tree). Keeps both bounded to the visible-dir count so
    // a long session over a big tree never accumulates stale snapshots.
    if (!m_cache.isEmpty()) {
        const QSet<QString> visibleSet(visible.cbegin(), visible.cend());
        for (auto it = m_cache.begin(); it != m_cache.end();) {
            if (!visibleSet.contains(it.key())) {
                it = m_cache.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Issue one readdir per visible dir, reusing the single SFTP session (the
    // PollFn posts onto the worker, which serializes). Skip a dir whose prior
    // poll hasn't returned yet so a slow link can't pile requests up.
    QPointer<RemoteDirectoryWatcher> self(this);
    for (const QString &dir : visible) {
        if (dir.isEmpty() || m_inFlight.contains(dir)) {
            continue;
        }
        m_inFlight.insert(dir);
        m_poll(dir, [self, dir](bool ok, const QList<DirEntry> &entries) {
            if (self) {
                self->onPolled(dir, ok, entries);
            }
        });
    }
}

void RemoteDirectoryWatcher::onPolled(const QString &path, bool ok,
                                      const QList<DirEntry> &entries)
{
    m_inFlight.remove(path);

    if (!ok) {
        // Transient readdir failure: leave the cached baseline intact so a blip
        // doesn't read as "all entries removed". The next successful poll
        // reconciles.
        return;
    }

    // Build the fresh snapshot keyed by name (O(entries)). Last-writer-wins on a
    // duplicate name, matching the model's name-keyed diff.
    QHash<QString, DirEntry> fresh;
    fresh.reserve(entries.size());
    for (const DirEntry &e : entries) {
        fresh.insert(e.name, e);
    }

    auto it = m_cache.find(path);
    if (it == m_cache.end()) {
        // First sighting: seed the baseline silently. The model already listed
        // this dir when it was expanded — that IS the baseline, not a change.
        m_cache.insert(path, fresh);
        return;
    }

    // QHash<QString,DirEntry>::operator== compares size + every key/value, so an
    // add, a remove, or a metadata change on any entry trips it. O(entries).
    if (it.value() != fresh) {
        it.value() = std::move(fresh);
        emit directoryChanged(path);
    }
}

} // namespace remote
