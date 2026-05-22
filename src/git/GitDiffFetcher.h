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

#ifndef GIT_DIFF_FETCHER_H
#define GIT_DIFF_FETCHER_H

#include "GitDiffCache.h"
#include "GitDiffParser.h"
#include "GitStatusEntry.h"

#include <QHash>
#include <QObject>
#include <QString>

#include <memory>

class GitController;

// Async wrapper around GitController::requestDiff that:
//   1) checks the LRU cache before issuing a process spawn
//   2) for untracked files, generates a synthetic diff from disk content
//   3) clears the cache when status changes
//
// Emits parsedReady when a parsed Result is ready to render (either from cache,
// from a completed git diff, or from on-disk synthesis).
class GitDiffFetcher : public QObject
{
    Q_OBJECT
public:
    explicit GitDiffFetcher(GitController *controller, QObject *parent = nullptr);

    // Request a diff for the given entry. Cancels any in-flight request and
    // emits parsedReady once for the new request.
    void request(const GitStatusEntry &entry);

    GitDiffCache *cache() { return &m_cache; }

signals:
    void parsedReady(const QString &relPath, bool stagedSide,
                     const std::shared_ptr<const GitDiffParser::Result> &parsed);
    void failed(const QString &relPath, bool stagedSide, const QString &message);

private slots:
    void onDiffReady(const QString &relPath, bool stagedSide, const QByteArray &diff);
    void onDiffFailed(const QString &relPath, bool stagedSide, const QString &message);
    void onStatusUpdated();

private:
    GitController *m_controller;
    GitDiffCache m_cache;

    struct Pending {
        bool stagedSide = false;
        bool isUntracked = false;
        GitStatusEntry entry;
    };
    QHash<QString, Pending> m_inflight;   // keyed by relPath

    // Generates a synthetic "all added" diff for an untracked file.
    static QByteArray synthDiffForUntracked(const QString &repoRoot, const QString &relPath);
};

#endif // GIT_DIFF_FETCHER_H
