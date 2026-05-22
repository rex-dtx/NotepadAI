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

#include "BranchRefParser.h"

BranchRefParser::Refs BranchRefParser::parse(const QByteArray &forEachRefOut,
                                             const QByteArray &headSymbolicOut,
                                             const QByteArray &headShaOut,
                                             const QByteArray &remotesOut)
{
    Refs r;

    // Remotes list
    for (const QByteArray &line : remotesOut.split('\n')) {
        const QString t = QString::fromUtf8(line).trimmed();
        if (!t.isEmpty() && !r.remotes.contains(t)) r.remotes.append(t);
    }

    // Refs: records are separated by trailing nul-newline pairs from for-each-ref's
    // --format. Each record is 4 nul-separated fields, then a literal newline.
    // We tokenise on nul and discard empty tokens — robust to either trailing nul or newline.
    QByteArray buf = forEachRefOut;
    // Normalize: drop CRs; turn newlines into nuls to make a single \0 stream.
    buf.replace('\r', "");
    buf.replace('\n', QByteArray("\0", 1));
    const QList<QByteArray> tokens = buf.split('\0');
    // Process in groups of 4 non-empty tokens — but we need to keep order. Walk
    // forward; when we hit "*" or " " for the HEAD marker we know a record starts.
    for (int i = 0; i + 3 < tokens.size(); ++i) {
        const QByteArray &h = tokens.at(i);
        if (h != "*" && h != " ") continue;
        const QString refName = QString::fromUtf8(tokens.at(i + 1));
        // const QString objType = QString::fromUtf8(tokens.at(i + 2));   // unused
        // const QString upstream = QString::fromUtf8(tokens.at(i + 3));  // unused
        if (refName.isEmpty()) { i += 3; continue; }

        // Filter: ignore symbolic refs like "origin/HEAD"
        if (refName.endsWith(QStringLiteral("/HEAD"))) { i += 3; continue; }

        const bool isRemote = std::any_of(r.remotes.cbegin(), r.remotes.cend(),
            [&](const QString &rem) { return refName.startsWith(rem + QLatin1Char('/')); });
        if (isRemote) {
            if (!r.remote.contains(refName)) r.remote.append(refName);
        } else {
            if (!r.local.contains(refName)) r.local.append(refName);
        }
        if (h == "*") r.currentLocal = refName;
        i += 3; // skip past this record
    }

    const QString headSym = QString::fromUtf8(headSymbolicOut).trimmed();
    const QString headSha = QString::fromUtf8(headShaOut).trimmed();

    if (headSym.isEmpty() && headSha.isEmpty()) {
        r.empty = true;
        r.currentLocal.clear();
        r.detachedShortSha.clear();
    } else if (headSym.isEmpty() && !headSha.isEmpty()) {
        r.currentLocal.clear();
        r.detachedShortSha = headSha;
    } else {
        r.currentLocal = headSym;
        r.detachedShortSha.clear();
    }

    std::sort(r.local.begin(), r.local.end());
    std::sort(r.remote.begin(), r.remote.end());
    return r;
}
