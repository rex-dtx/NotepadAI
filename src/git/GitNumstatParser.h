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

#ifndef GIT_NUMSTAT_PARSER_H
#define GIT_NUMSTAT_PARSER_H

#include <QByteArray>
#include <QHash>
#include <QString>

#include <cstdint>

// Parses output of `git diff --numstat -z [--cached]`.
//
// Format per record (NUL-separated):
//   added\tdeleted\tpath\0                  (normal record)
//   added\tdeleted\t\0orig\0new\0           (renamed/copied record — leading
//                                            path field empty, then orig + new
//                                            paths)
//   -\t-\tpath\0                            (binary file)
//
// Output keyed by the *new* path. Linear single-pass, O(N) bytes.
class GitNumstatParser
{
public:
    struct Stat {
        qint32 added = -1;
        qint32 deleted = -1;
        bool isBinary = false;
    };

    static QHash<QString, Stat> parse(const QByteArray &input);
};

#endif // GIT_NUMSTAT_PARSER_H
