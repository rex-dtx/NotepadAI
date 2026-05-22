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

#ifndef BRANCH_REF_PARSER_H
#define BRANCH_REF_PARSER_H

#include <QByteArray>
#include <QString>
#include <QStringList>

class BranchRefParser
{
public:
    struct Refs {
        QStringList local;
        QStringList remote;
        QString currentLocal;         // empty if detached or empty repo
        QString detachedShortSha;     // empty unless detached
        QStringList remotes;          // ["origin","upstream",...]
        bool empty = false;
    };

    // forEachRefOut produced by:
    //   git for-each-ref --format='%(HEAD)%00%(refname:short)%00%(objecttype)%00%(upstream:short)%00'
    //                    refs/heads refs/remotes
    // headSymbolicOut from `git symbolic-ref --short -q HEAD` (empty on detached)
    // headShaOut from `git rev-parse --short HEAD` (empty on empty repo)
    // remotesOut from `git remote`
    static Refs parse(const QByteArray &forEachRefOut,
                      const QByteArray &headSymbolicOut,
                      const QByteArray &headShaOut,
                      const QByteArray &remotesOut);
};

#endif // BRANCH_REF_PARSER_H
