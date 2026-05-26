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

#ifndef CONFLICT_ENTRY_H
#define CONFLICT_ENTRY_H

#include <QString>
#include <QVector>

#include <cstdint>

struct ConflictEntry
{
    enum Type : std::uint8_t {
        BothModified,
        ModifiedDeleted,
        DeletedModified,
        BothAdded
    };

    QString relPath;
    Type type = BothModified;
    QString baseSha;
    QString oursSha;
    QString theirsSha;
};

using ConflictEntries = QVector<ConflictEntry>;

struct ConflictData
{
    QByteArray baseContent;
    QByteArray oursContent;
    QByteArray theirsContent;
    QString filePath;
    bool isReversed = false;
    QString leftLabel;
    QString rightLabel;
};

#endif // CONFLICT_ENTRY_H
