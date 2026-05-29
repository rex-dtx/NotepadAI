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

#ifndef RECURSIVE_TREE_WATCHER_H
#define RECURSIVE_TREE_WATCHER_H

#include <QtGlobal>

#ifdef Q_OS_WIN

#include <QObject>
#include <QString>
#include <QThread>

#include <atomic>

// Watches a directory tree recursively on Windows using ReadDirectoryChangesW.
// Emits changed() (delivered on the GUI thread) when any file in the tree is
// created, deleted, renamed, or has its last-write time modified.
// Notifications originating from the .git/ subtree are filtered out.
class RecursiveTreeWatcher : public QObject
{
    Q_OBJECT
public:
    explicit RecursiveTreeWatcher(QObject *parent = nullptr);
    ~RecursiveTreeWatcher() override;

    void start(const QString &path);
    void stop();
    void ackNotify() { m_pendingNotify.store(false, std::memory_order_release); }
    bool isRunning() const { return m_thread != nullptr && m_thread->isRunning(); }

signals:
    void changed();

private:
    QThread *m_thread = nullptr;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_pendingNotify{false};
    void *m_stopEvent = nullptr; // HANDLE
};

#endif // Q_OS_WIN
#endif // RECURSIVE_TREE_WATCHER_H
