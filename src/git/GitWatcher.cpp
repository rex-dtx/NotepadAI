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

#include "GitWatcher.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>

GitWatcher::GitWatcher(QObject *parent) : QObject(parent)
{
    m_fs = new QFileSystemWatcher(this);
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(200);

    connect(m_fs, &QFileSystemWatcher::fileChanged, this, &GitWatcher::onFileChanged);
    connect(m_fs, &QFileSystemWatcher::directoryChanged, this, &GitWatcher::onDirChanged);
    connect(m_debounce, &QTimer::timeout, this, &GitWatcher::onDebounce);
}

GitWatcher::~GitWatcher() = default;

void GitWatcher::clear()
{
    if (!m_fs->files().isEmpty()) m_fs->removePaths(m_fs->files());
    if (!m_fs->directories().isEmpty()) m_fs->removePaths(m_fs->directories());
    m_repoRoot.clear();
    m_gitDir.clear();
    m_pending = 0;
}

void GitWatcher::setRepo(const QString &toplevel)
{
    clear();
    if (toplevel.isEmpty()) return;
    m_repoRoot = QDir::cleanPath(toplevel);

    // .git may be a directory (normal repo) or a file containing "gitdir: <path>"
    // (worktree or submodule). Resolve it.
    const QString dotGitEntry = m_repoRoot + QStringLiteral("/.git");
    QFileInfo fi(dotGitEntry);
    if (fi.isDir()) {
        m_gitDir = dotGitEntry;
    } else if (fi.isFile()) {
        QFile f(dotGitEntry);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString contents = QString::fromUtf8(f.readAll()).trimmed();
            const QString prefix = QStringLiteral("gitdir: ");
            if (contents.startsWith(prefix)) {
                QString gitDir = contents.mid(prefix.size()).trimmed();
                if (!QFileInfo(gitDir).isAbsolute())
                    gitDir = QDir::cleanPath(m_repoRoot + QLatin1Char('/') + gitDir);
                m_gitDir = gitDir;
            }
        }
    }
    if (m_gitDir.isEmpty()) m_gitDir = dotGitEntry; // best-effort

    rewatch();
}

QStringList GitWatcher::currentWatchedFiles() const
{
    QStringList files;
    if (m_gitDir.isEmpty()) return files;
    files.append(m_gitDir + QStringLiteral("/HEAD"));
    files.append(m_gitDir + QStringLiteral("/index"));
    files.append(m_gitDir + QStringLiteral("/packed-refs"));
    // Trim missing files; QFileSystemWatcher will warn loudly on misses.
    QStringList existing;
    for (const QString &f : files)
        if (QFile::exists(f)) existing.append(f);
    return existing;
}

QStringList GitWatcher::currentWatchedDirs() const
{
    QStringList dirs;
    if (m_gitDir.isEmpty()) return dirs;
    dirs.append(m_gitDir + QStringLiteral("/refs/heads"));
    dirs.append(m_gitDir + QStringLiteral("/refs/remotes"));
    dirs.append(m_repoRoot);
    QStringList existing;
    for (const QString &d : dirs)
        if (QFileInfo(d).isDir()) existing.append(d);
    return existing;
}

void GitWatcher::rewatch()
{
    const QStringList files = currentWatchedFiles();
    const QStringList dirs = currentWatchedDirs();
    if (!files.isEmpty()) m_fs->addPaths(files);
    if (!dirs.isEmpty()) m_fs->addPaths(dirs);
}

void GitWatcher::onFileChanged(const QString &path)
{
    if (path.endsWith(QStringLiteral("/HEAD")))         m_pending |= PHead;
    else if (path.endsWith(QStringLiteral("/index")))   m_pending |= PIndex;
    else if (path.endsWith(QStringLiteral("/packed-refs"))) m_pending |= PRefs;
    else                                                m_pending |= PTree;
    // QFileSystemWatcher stops watching a file after it's atomically replaced
    // (common with git); re-add to keep tracking.
    if (!m_fs->files().contains(path) && QFile::exists(path)) m_fs->addPath(path);
    m_debounce->start();
}

void GitWatcher::onDirChanged(const QString &path)
{
    if (path.endsWith(QStringLiteral("/refs/heads")) ||
        path.endsWith(QStringLiteral("/refs/remotes"))) {
        m_pending |= PRefs;
    } else {
        m_pending |= PTree;
    }
    m_debounce->start();
}

void GitWatcher::onDebounce()
{
    const int p = m_pending;
    m_pending = 0;
    if (p & PHead)  emit headChanged();
    if (p & PIndex) emit indexChanged();
    if (p & PRefs)  emit refsChanged();
    if (p & PTree)  emit workingTreeChanged();
}
