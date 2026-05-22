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

#ifndef GIT_STATUS_PARSER_H
#define GIT_STATUS_PARSER_H

#include "GitStatusEntry.h"

#include <QByteArray>

// Parses the output of `git status --porcelain=v2 -z --untracked-files=all --renames`.
// The -z flag makes paths nul-terminated and unquoted (no octal escapes).
//
// Record types:
//   "1 XY sub mH mI mW hH hI path"                      ordinary changed
//   "2 XY sub mH mI mW hH hI X<score> path\0origPath"   renamed/copied
//   "u XY sub m1 m2 m3 mW h1 h2 h3 path"                unmerged (conflict)
//   "? path"                                            untracked
//   "! path"                                            ignored (we drop)
class GitStatusParser
{
public:
    static GitStatusEntries parsePorcelainV2(const QByteArray &nulSeparated);

    // Public for tests.
    static GitStatusEntry::Change xyToChange(char x, char y, bool stagedSide);
    static bool isUnmerged(char x, char y);
};

#endif // GIT_STATUS_PARSER_H
