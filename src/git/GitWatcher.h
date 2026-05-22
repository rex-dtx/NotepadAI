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

#ifndef GIT_WATCHER_H
#define GIT_WATCHER_H

#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>

class QFileSystemWatcher;
class QTimer;

// Watches a repo's .git/ control files plus the working tree top level.
// Coalesces flurries of events into one of four debounced signals.
class GitWatcher : public QObject
{
    Q_OBJECT
public:
    explicit GitWatcher(QObject *parent = nullptr);
    ~GitWatcher() override;

    void setRepo(const QString &toplevel);
    void clear();

signals:
    void headChanged();
    void indexChanged();
    void refsChanged();
    void workingTreeChanged();

private slots:
    void onFileChanged(const QString &path);
    void onDirChanged(const QString &path);
    void onDebounce();

private:
    QFileSystemWatcher *m_fs = nullptr;
    QTimer *m_debounce = nullptr;
    QString m_repoRoot;
    QString m_gitDir;
    int m_pending = 0;

    enum Pending : std::uint8_t { PHead = 1, PIndex = 2, PRefs = 4, PTree = 8 };

    void rewatch();
    QStringList currentWatchedFiles() const;
    QStringList currentWatchedDirs() const;
};

#endif // GIT_WATCHER_H
