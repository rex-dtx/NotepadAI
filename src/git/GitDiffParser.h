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

#ifndef GIT_DIFF_PARSER_H
#define GIT_DIFF_PARSER_H

#include <QByteArray>
#include <QString>
#include <QVector>

#include <cstdint>

// Unified-diff parser producing flat POD parallel arrays — cache friendly for
// the downstream painter, which walks the arrays in lock-step and pushes whole
// blocks to Scintilla via SCI_SETSTYLINGEX. Each row corresponds to ONE display
// line in the final Scintilla buffer (header rows have no source line numbers).
class GitDiffParser
{
public:
    enum class LineKind : std::uint8_t {
        FileHeader,   // "diff --git ...", "--- a/...", "+++ b/...", binary banner
        HunkHeader,   // "@@ -O,L +N,L @@"
        Context,      // " " prefix
        Added,        // "+" prefix
        Deleted,      // "-" prefix
        NoNewline,    // "\ No newline at end of file"
        Empty         // separator (e.g. between files in a combined patch)
    };

    struct Result {
        // Parallel arrays (length == row count).
        QVector<QByteArray> texts;   // line text WITHOUT prefix (we add it back in display)
        QVector<LineKind>   kinds;
        QVector<qint32>     oldLn;   // -1 = no source line
        QVector<qint32>     newLn;

        // Statistics
        qint32 addedCount = 0;
        qint32 deletedCount = 0;
        bool   isBinary = false;
    };

    static Result parse(const QByteArray &diff);
};

#endif // GIT_DIFF_PARSER_H
