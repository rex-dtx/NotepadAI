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

#ifndef GIT_STATUS_ENTRY_H
#define GIT_STATUS_ENTRY_H

#include <QString>
#include <QVector>

#include <cstdint>

struct GitStatusEntry
{
    enum Section : std::uint8_t { Conflicts = 0, Staged, Tracked, Untracked, SectionCount };
    enum Change : std::uint8_t { Added, Modified, Deleted, Renamed, Copied, TypeChanged, Untracked_, Unmerged };

    Section section = Tracked;
    Change change = Modified;
    QString relPath;
    QString origRelPath;
    bool stagedSide = false;
    QString xy;

    // Numstat (filled lazily by GitController numstat ops). -1 = not yet known.
    // For binary files, isBinary=true and both line counts stay at -1.
    qint32 addedLines = -1;
    qint32 deletedLines = -1;
    bool isBinary = false;
    bool isModeOnly = false;            // mode-only change (no content delta)
    bool hasUnstableEncoding = false;   // raw path bytes aren't valid UTF-8

    // For unmerged ('u') records: blob shas of ours / theirs sides.
    QString oursSha;
    QString theirsSha;
};

using GitStatusEntries = QVector<GitStatusEntry>;

#endif // GIT_STATUS_ENTRY_H
