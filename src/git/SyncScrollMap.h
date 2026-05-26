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

#ifndef SYNC_SCROLL_MAP_H
#define SYNC_SCROLL_MAP_H

#include "BufferDiffEngine.h"

#include <QVector>

#include <cstdint>

enum class SyncPanel : std::uint8_t { Left, Center, Right };

class SyncScrollMap
{
public:
    struct Anchor {
        qint32 left;
        qint32 center;
        qint32 right;
    };

    SyncScrollMap() = default;

    void build(const BufferDiffEngine::Hunks &leftToCenter,
               const BufferDiffEngine::Hunks &rightToCenter,
               qint32 leftLineCount,
               qint32 centerLineCount,
               qint32 rightLineCount);

    qint32 translate(SyncPanel from, qint32 line, SyncPanel to) const;

    bool isEmpty() const { return m_anchors.isEmpty(); }
    const QVector<Anchor> &anchors() const { return m_anchors; }

private:
    QVector<Anchor> m_anchors;
};

#endif // SYNC_SCROLL_MAP_H
