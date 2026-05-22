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

#include "GitStatusParser.h"

#include <QList>

namespace {

// Split on first N spaces, returning the trailing chunk verbatim. For porcelain
// v2 we count from the left because field counts are fixed per record type.
QList<QByteArray> splitFields(const QByteArray &line, int firstN)
{
    QList<QByteArray> out;
    int start = 0;
    int field = 0;
    for (int i = 0; i < line.size() && field < firstN; ++i) {
        if (line[i] == ' ') {
            out.append(line.mid(start, i - start));
            start = i + 1;
            ++field;
        }
    }
    out.append(line.mid(start));
    return out;
}

GitStatusEntry makeEntry(GitStatusEntry::Section sec,
                         GitStatusEntry::Change ch,
                         const QString &path,
                         const QString &orig,
                         bool stagedSide,
                         const QString &xy)
{
    GitStatusEntry e;
    e.section = sec;
    e.change = ch;
    e.relPath = path;
    e.origRelPath = orig;
    e.stagedSide = stagedSide;
    e.xy = xy;
    return e;
}

} // namespace

bool GitStatusParser::isUnmerged(char x, char y)
{
    // From git status docs: any of DD AU UD UA DU AA UU
    if (x == 'U' || y == 'U') return true;
    if (x == 'A' && y == 'A') return true;
    if (x == 'D' && y == 'D') return true;
    return false;
}

GitStatusEntry::Change GitStatusParser::xyToChange(char x, char y, bool stagedSide)
{
    const char c = stagedSide ? x : y;
    switch (c) {
        case 'A': return GitStatusEntry::Added;
        case 'M': return GitStatusEntry::Modified;
        case 'D': return GitStatusEntry::Deleted;
        case 'R': return GitStatusEntry::Renamed;
        case 'C': return GitStatusEntry::Copied;
        case 'T': return GitStatusEntry::TypeChanged;
        case '?': return GitStatusEntry::Untracked_;
        case 'U': return GitStatusEntry::Unmerged;
        default:  return GitStatusEntry::Modified;
    }
}

GitStatusEntries GitStatusParser::parsePorcelainV2(const QByteArray &input)
{
    GitStatusEntries result;

    // Records are nul-separated. Renamed records contain TWO paths separated by nul,
    // so we walk byte-by-byte and parse record-by-record based on the leading marker.
    int i = 0;
    const int n = input.size();
    while (i < n) {
        // Locate end of this record's first segment (header + path), terminated by \0.
        int end = input.indexOf('\0', i);
        if (end < 0) end = n;
        QByteArray rec = input.mid(i, end - i);
        i = end + 1;
        if (rec.isEmpty()) continue;

        const char marker = rec[0];

        if (marker == '1') {
            // "1 XY sub mH mI mW hH hI path"
            // path is field index 8 (zero-based)
            QList<QByteArray> fields = splitFields(rec, 8);
            if (fields.size() < 9) continue;
            const QByteArray &xyB = fields.at(1);
            const QString path = QString::fromUtf8(fields.at(8));
            if (xyB.size() < 2) continue;
            const char x = xyB.at(0);
            const char y = xyB.at(1);
            const QString xy = QString::fromLatin1(xyB);

            if (x != '.' && x != ' ') {
                result.append(makeEntry(GitStatusEntry::Staged,
                                        xyToChange(x, y, true),
                                        path, {}, true, xy));
            }
            if (y != '.' && y != ' ') {
                result.append(makeEntry(GitStatusEntry::Tracked,
                                        xyToChange(x, y, false),
                                        path, {}, false, xy));
            }
        }
        else if (marker == '2') {
            // "2 XY sub mH mI mW hH hI X<score> path\0origPath"
            // path is field index 9; orig path follows after a nul terminator.
            QList<QByteArray> fields = splitFields(rec, 9);
            if (fields.size() < 10) continue;
            const QByteArray &xyB = fields.at(1);
            const QString path = QString::fromUtf8(fields.at(9));
            // Read the orig path (next nul-terminated segment).
            int origEnd = input.indexOf('\0', i);
            if (origEnd < 0) origEnd = n;
            const QString orig = QString::fromUtf8(input.mid(i, origEnd - i));
            i = origEnd + 1;
            if (xyB.size() < 2) continue;
            const char x = xyB.at(0);
            const char y = xyB.at(1);
            const QString xy = QString::fromLatin1(xyB);

            if (x != '.' && x != ' ') {
                result.append(makeEntry(GitStatusEntry::Staged,
                                        xyToChange(x, y, true),
                                        path, orig, true, xy));
            }
            if (y != '.' && y != ' ') {
                result.append(makeEntry(GitStatusEntry::Tracked,
                                        xyToChange(x, y, false),
                                        path, orig, false, xy));
            }
        }
        else if (marker == 'u') {
            // "u XY sub m1 m2 m3 mW h1 h2 h3 path"
            QList<QByteArray> fields = splitFields(rec, 10);
            if (fields.size() < 11) continue;
            const QByteArray &xyB = fields.at(1);
            const QString path = QString::fromUtf8(fields.at(10));
            const QString xy = QString::fromLatin1(xyB);
            result.append(makeEntry(GitStatusEntry::Conflicts,
                                    GitStatusEntry::Unmerged,
                                    path, {}, false, xy));
        }
        else if (marker == '?') {
            // "? path"
            QByteArray path = rec.mid(2);
            result.append(makeEntry(GitStatusEntry::Untracked,
                                    GitStatusEntry::Untracked_,
                                    QString::fromUtf8(path), {}, false,
                                    QStringLiteral("??")));
        }
        else if (marker == '#' || marker == '!') {
            // header line or ignored — drop
            continue;
        }
    }
    return result;
}
