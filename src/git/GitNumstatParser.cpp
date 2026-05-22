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

#include "GitNumstatParser.h"

namespace {

inline qint32 parseSmallInt(const char *data, qsizetype size)
{
    if (size == 1 && data[0] == '-') return -1;
    qint32 v = 0;
    for (qsizetype k = 0; k < size; ++k) {
        const char c = data[k];
        if (c < '0' || c > '9') return -1;
        v = v * 10 + (c - '0');
    }
    return v;
}

} // namespace

QHash<QString, GitNumstatParser::Stat> GitNumstatParser::parse(const QByteArray &input)
{
    QHash<QString, Stat> out;
    out.reserve(64);

    const char *data = input.constData();
    const qsizetype n = input.size();
    qsizetype i = 0;

    while (i < n) {
        // Each record begins with "added\tdeleted\t" then either:
        //   - "path\0"                   (normal)
        //   - "\0orig\0new\0"            (renamed: empty path then two NUL'd paths)
        const qsizetype t1 = input.indexOf('\t', i);
        if (t1 < 0) break;
        const qsizetype t2 = input.indexOf('\t', t1 + 1);
        if (t2 < 0) break;

        const qsizetype addedLen = t1 - i;
        const qsizetype delLen = t2 - t1 - 1;

        Stat s;
        if (addedLen == 1 && data[i] == '-') {
            s.isBinary = true;
            s.added = -1;
            s.deleted = -1;
        } else {
            s.added = parseSmallInt(data + i, addedLen);
            s.deleted = parseSmallInt(data + t1 + 1, delLen);
        }

        qsizetype p = t2 + 1;
        // Look at the byte right after the second tab.
        if (p < n && data[p] == '\0') {
            // Renamed: skip empty path, then read orig, then new path.
            ++p; // consume the empty-path nul
            const qsizetype origEnd = input.indexOf('\0', p);
            if (origEnd < 0) break;
            p = origEnd + 1;
            const qsizetype newEnd = input.indexOf('\0', p);
            if (newEnd < 0) break;
            const QString newPath = QString::fromUtf8(data + p, newEnd - p);
            out.insert(newPath, s);
            i = newEnd + 1;
        } else {
            const qsizetype pathEnd = input.indexOf('\0', p);
            const qsizetype end = (pathEnd < 0) ? n : pathEnd;
            const QString path = QString::fromUtf8(data + p, end - p);
            out.insert(path, s);
            i = end + 1;
        }
    }
    return out;
}
