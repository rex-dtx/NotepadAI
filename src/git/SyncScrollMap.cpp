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

#include "SyncScrollMap.h"

#include <algorithm>

static qint32 interpolate(const QVector<SyncScrollMap::Anchor> &anchors,
                           qint32 fromLine,
                           qint32 SyncScrollMap::Anchor::*fromField,
                           qint32 SyncScrollMap::Anchor::*toField)
{
    if (anchors.isEmpty()) return fromLine;
    if (anchors.size() == 1) return anchors[0].*toField;

    auto it = std::upper_bound(anchors.begin(), anchors.end(), fromLine,
        [&](qint32 val, const SyncScrollMap::Anchor &a) { return val < a.*fromField; });

    if (it == anchors.begin()) {
        return anchors.front().*toField + (fromLine - anchors.front().*fromField);
    }
    if (it == anchors.end()) {
        const auto &last = anchors.back();
        return last.*toField + (fromLine - last.*fromField);
    }

    const auto &prev = *(it - 1);
    const auto &next = *it;
    qint32 dFrom = next.*fromField - prev.*fromField;
    if (dFrom == 0) return prev.*toField;
    qint32 dTo = next.*toField - prev.*toField;
    return prev.*toField + (fromLine - prev.*fromField) * dTo / dFrom;
}

void SyncScrollMap::build(const BufferDiffEngine::Hunks &leftToCenter,
                          const BufferDiffEngine::Hunks &rightToCenter,
                          qint32 leftLineCount,
                          qint32 centerLineCount,
                          qint32 rightLineCount)
{
    m_anchors.clear();

    struct SidePt { qint32 sideLine; qint32 centerLine; };
    QVector<SidePt> leftPts, rightPts;

    leftPts.reserve(leftToCenter.size() * 2 + 2);
    rightPts.reserve(rightToCenter.size() * 2 + 2);

    leftPts.append({0, 0});
    for (const auto &h : leftToCenter) {
        leftPts.append({h.oldStart, h.newStart});
        leftPts.append({h.oldStart + h.oldCount, h.newStart + h.newCount});
    }
    leftPts.append({leftLineCount, centerLineCount});

    rightPts.append({0, 0});
    for (const auto &h : rightToCenter) {
        rightPts.append({h.oldStart, h.newStart});
        rightPts.append({h.oldStart + h.oldCount, h.newStart + h.newCount});
    }
    rightPts.append({rightLineCount, centerLineCount});

    auto leftCenterToSide = [&](qint32 cLine) -> qint32 {
        auto it = std::upper_bound(leftPts.begin(), leftPts.end(), cLine,
            [](qint32 val, const SidePt &p) { return val < p.centerLine; });
        if (it == leftPts.begin()) return leftPts.front().sideLine;
        if (it == leftPts.end()) {
            const auto &last = leftPts.back();
            return last.sideLine + (cLine - last.centerLine);
        }
        const auto &prev = *(it - 1);
        const auto &next = *it;
        qint32 dc = next.centerLine - prev.centerLine;
        if (dc == 0) return prev.sideLine;
        return prev.sideLine + (cLine - prev.centerLine) * (next.sideLine - prev.sideLine) / dc;
    };

    auto rightCenterToSide = [&](qint32 cLine) -> qint32 {
        auto it = std::upper_bound(rightPts.begin(), rightPts.end(), cLine,
            [](qint32 val, const SidePt &p) { return val < p.centerLine; });
        if (it == rightPts.begin()) return rightPts.front().sideLine;
        if (it == rightPts.end()) {
            const auto &last = rightPts.back();
            return last.sideLine + (cLine - last.centerLine);
        }
        const auto &prev = *(it - 1);
        const auto &next = *it;
        qint32 dc = next.centerLine - prev.centerLine;
        if (dc == 0) return prev.sideLine;
        return prev.sideLine + (cLine - prev.centerLine) * (next.sideLine - prev.sideLine) / dc;
    };

    QVector<qint32> centerLines;
    centerLines.reserve(leftPts.size() + rightPts.size());
    for (const auto &p : leftPts) centerLines.append(p.centerLine);
    for (const auto &p : rightPts) centerLines.append(p.centerLine);
    std::sort(centerLines.begin(), centerLines.end());
    centerLines.erase(std::unique(centerLines.begin(), centerLines.end()), centerLines.end());

    m_anchors.reserve(centerLines.size());
    for (qint32 c : centerLines) {
        m_anchors.append({leftCenterToSide(c), c, rightCenterToSide(c)});
    }
}

qint32 SyncScrollMap::translate(SyncPanel from, qint32 line, SyncPanel to) const
{
    if (from == to || m_anchors.isEmpty()) return line;

    qint32 Anchor::*fromField = &Anchor::center;
    qint32 Anchor::*toField = &Anchor::center;

    switch (from) {
    case SyncPanel::Left:   fromField = &Anchor::left;   break;
    case SyncPanel::Center: fromField = &Anchor::center; break;
    case SyncPanel::Right:  fromField = &Anchor::right;  break;
    }
    switch (to) {
    case SyncPanel::Left:   toField = &Anchor::left;   break;
    case SyncPanel::Center: toField = &Anchor::center; break;
    case SyncPanel::Right:  toField = &Anchor::right;  break;
    }

    return interpolate(m_anchors, line, fromField, toField);
}
