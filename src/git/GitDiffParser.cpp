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

#include "GitDiffParser.h"

#include <cstring>

namespace {

// Parse decimal integer from [p, q). Returns -1 on empty or non-digit.
inline qint32 parseDec(const char *p, const char *q)
{
    if (p == q) return -1;
    qint32 v = 0;
    while (p < q) {
        const char c = *p++;
        if (c < '0' || c > '9') return -1;
        v = v * 10 + (c - '0');
    }
    return v;
}

// Parse hunk header "@@ -O[,L] +N[,L] @@ ...". Writes oldStart/newStart out.
// Returns true if parsed. We don't actually need the lengths — we re-derive
// counts by walking lines.
bool parseHunkHeader(const char *p, const char *end, qint32 &oldStart, qint32 &newStart)
{
    if (end - p < 8) return false;
    if (p[0] != '@' || p[1] != '@' || p[2] != ' ') return false;
    p += 3;
    if (*p != '-') return false;
    ++p;
    const char *commaOrSpace = p;
    while (commaOrSpace < end && *commaOrSpace != ',' && *commaOrSpace != ' ') ++commaOrSpace;
    oldStart = parseDec(p, commaOrSpace);
    if (oldStart < 0) return false;
    // Skip to next '+'
    while (p < end && *p != '+') ++p;
    if (p >= end || *p != '+') return false;
    ++p;
    const char *commaOrSpace2 = p;
    while (commaOrSpace2 < end && *commaOrSpace2 != ',' && *commaOrSpace2 != ' ') ++commaOrSpace2;
    newStart = parseDec(p, commaOrSpace2);
    return newStart >= 0;
}

} // namespace

GitDiffParser::Result GitDiffParser::parse(const QByteArray &diff)
{
    Result r;
    const char *data = diff.constData();
    const qsizetype n = diff.size();
    if (n == 0) return r;

    // Pre-reserve based on a coarse estimate (assume ~50 bytes/line).
    const qsizetype estLines = n / 50 + 8;
    r.texts.reserve(estLines);
    r.kinds.reserve(estLines);
    r.oldLn.reserve(estLines);
    r.newLn.reserve(estLines);

    qint32 curOld = -1;
    qint32 curNew = -1;
    bool inHunk = false;
    bool sawBinary = false;

    qsizetype i = 0;
    while (i < n) {
        // Find end of line (LF). Handle \r\n by trimming trailing \r.
        qsizetype j = i;
        while (j < n && data[j] != '\n') ++j;
        qsizetype lineEnd = j;
        if (lineEnd > i && data[lineEnd - 1] == '\r') --lineEnd;

        const char *p = data + i;
        const qsizetype len = lineEnd - i;
        const char first = (len > 0) ? *p : '\0';

        LineKind kind;
        qint32 oLn = -1, nLn = -1;
        qsizetype textStart = i;
        qsizetype textLen = len;

        if (len >= 4 && p[0] == '@' && p[1] == '@') {
            qint32 os = -1, ns = -1;
            if (parseHunkHeader(p, p + len, os, ns)) {
                curOld = os;
                curNew = ns;
                inHunk = true;
            }
            kind = LineKind::HunkHeader;
        }
        else if (len == 0) {
            kind = LineKind::Empty;
            inHunk = false;
        }
        else if (!inHunk) {
            // File header region: "diff --git", "index", "---", "+++", "Binary files differ", etc.
            kind = LineKind::FileHeader;
            if (len >= 7 && std::memcmp(p, "Binary ", 7) == 0) {
                sawBinary = true;
            }
        }
        else if (first == '+') {
            // Skip "+++ " inside a header? Already handled above. In hunk, '+' = added.
            if (len >= 4 && p[0] == '+' && p[1] == '+' && p[2] == '+' && p[3] == ' ') {
                kind = LineKind::FileHeader;
                inHunk = false;
            } else {
                kind = LineKind::Added;
                textStart = i + 1;
                textLen = len - 1;
                nLn = curNew++;
                ++r.addedCount;
            }
        }
        else if (first == '-') {
            if (len >= 4 && p[0] == '-' && p[1] == '-' && p[2] == '-' && p[3] == ' ') {
                kind = LineKind::FileHeader;
                inHunk = false;
            } else {
                kind = LineKind::Deleted;
                textStart = i + 1;
                textLen = len - 1;
                oLn = curOld++;
                ++r.deletedCount;
            }
        }
        else if (first == ' ') {
            kind = LineKind::Context;
            textStart = i + 1;
            textLen = len - 1;
            oLn = curOld++;
            nLn = curNew++;
        }
        else if (first == '\\') {
            kind = LineKind::NoNewline;
        }
        else if (first == 'd' && len >= 4 && std::memcmp(p, "diff", 4) == 0) {
            kind = LineKind::FileHeader;
            inHunk = false;
            // Reset for next file
            curOld = curNew = -1;
        }
        else {
            kind = LineKind::FileHeader;
        }

        r.texts.append(QByteArray(data + textStart, static_cast<int>(textLen)));
        r.kinds.append(kind);
        r.oldLn.append(oLn);
        r.newLn.append(nLn);

        i = j + 1; // step past the LF (or off end)
    }

    r.isBinary = sawBinary;
    return r;
}
