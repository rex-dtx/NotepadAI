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

#include "TerminalAiHelper.h"

#include <algorithm>

QString wrapInCodeblock(const QString &text)
{
    const QChar *data = text.constData();
    const int len = text.size();

    int maxRun = 0;
    int cur = 0;
    for (int i = 0; i < len; ++i) {
        if (data[i] == QLatin1Char('`')) {
            ++cur;
            if (cur > maxRun) maxRun = cur;
        } else {
            cur = 0;
        }
    }

    const int fenceLen = std::max(3, maxRun + 1);
    const bool needsTrailingNewline = (len == 0 || data[len - 1] != QLatin1Char('\n'));
    const int totalSize = fenceLen + 1 + len + (needsTrailingNewline ? 1 : 0) + fenceLen + 1;

    QString result;
    result.reserve(totalSize);
    result.append(QString(fenceLen, QLatin1Char('`')));
    result.append(QLatin1Char('\n'));
    result.append(text);
    if (needsTrailingNewline) result.append(QLatin1Char('\n'));
    result.append(QString(fenceLen, QLatin1Char('`')));
    result.append(QLatin1Char('\n'));
    return result;
}
