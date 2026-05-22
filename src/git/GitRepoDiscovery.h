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

#ifndef GIT_REPO_DISCOVERY_H
#define GIT_REPO_DISCOVERY_H

#include "GitRepoInfo.h"

#include <QByteArray>
#include <QString>

class GitRepoDiscovery
{
public:
    // Parses output of `git submodule status --recursive`. Each line:
    //   " 1234abcd path/to/sub (heads/main)"     # in sync
    //   "+1234abcd path/to/sub (heads/main)"     # different commit checked out
    //   "-1234abcd path/to/sub"                  # not initialised
    //   "U1234abcd path/to/sub"                  # merge conflict
    // "path/to/sub" is RELATIVE to the root repo.
    static GitRepoInfos parseSubmoduleStatus(const QByteArray &out, const QString &rootToplevel);
};

#endif // GIT_REPO_DISCOVERY_H
