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

#include "GitRepoDiscovery.h"

#include <QDir>
#include <QFileInfo>

GitRepoInfos GitRepoDiscovery::parseSubmoduleStatus(const QByteArray &out, const QString &rootToplevel)
{
    GitRepoInfos result;
    const QString rootClean = QDir::cleanPath(rootToplevel);

    const QList<QByteArray> lines = out.split('\n');
    for (const QByteArray &raw : lines) {
        QString line = QString::fromUtf8(raw);
        if (line.isEmpty()) continue;

        // first char is status marker (' ', '+', '-', 'U'), skip it
        line = line.mid(1);
        // next: 40-char sha + space, skip 41 chars
        if (line.size() < 41) continue;
        line = line.mid(41);
        // trim trailing " (ref)" annotation
        const int paren = line.lastIndexOf(QLatin1Char(' '));
        if (paren > 0 && line.endsWith(QLatin1Char(')')))
            line = line.left(paren);

        const QString relPath = line.trimmed();
        if (relPath.isEmpty()) continue;

        const QString abs = QDir::cleanPath(rootClean + QLatin1Char('/') + relPath);
        const int depth = relPath.count(QLatin1Char('/')) + 1;

        GitRepoInfo info;
        info.toplevel = abs;
        info.displayName = relPath;
        info.depth = depth;
        info.isSubmodule = true;
        result.append(info);
    }
    return result;
}
