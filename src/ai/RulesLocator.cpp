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

#include "RulesLocator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace ai {

QStringList RulesLocator::probeNames()
{
    return { QStringLiteral("CLAUDE.md"),
             QStringLiteral("AGENTS.md"),
             QStringLiteral(".rules") };
}

QByteArray RulesLocator::readIfExists(const QString &path)
{
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

QByteArray RulesLocator::truncateToBudget(const QByteArray &content, int budgetBytes)
{
    if (budgetBytes <= 0 || content.size() <= budgetBytes) return content;
    QByteArray marker = QByteArrayLiteral("\n... [project rules truncated]\n");
    const int target = budgetBytes - marker.size();
    if (target <= 0) return marker;

    // Find last '\n' at or below `target` so we cut on a line boundary.
    int cut = target;
    while (cut > 0 && content.at(cut) != '\n') --cut;
    if (cut == 0) {
        // No newline within budget — cut at target byte (may split a UTF-8
        // multi-byte char, so walk back to a boundary).
        cut = target;
        while (cut > 0 && (static_cast<unsigned char>(content.at(cut)) & 0xC0) == 0x80) {
            --cut;
        }
    }

    QByteArray out;
    out.reserve(cut + marker.size());
    out.append(content.constData(), cut);
    out.append(marker);
    return out;
}

namespace {

QByteArray probeRoot(const QString &root, const QStringList &names)
{
    if (root.isEmpty()) return {};
    QFileInfo rootInfo(root);
    if (!rootInfo.isDir()) return {};
    QDir dir(root);
    for (const QString &name : names) {
        const QString path = dir.filePath(name);
        const QByteArray content = RulesLocator::readIfExists(path);
        if (QString::fromUtf8(content).trimmed().isEmpty()) continue;
        return content;
    }
    return {};
}

} // namespace

QString RulesLocator::locate(const QString &submoduleRoot,
                             const QString &workspaceRoot,
                             int budgetBytes)
{
    const QStringList names = probeNames();

    QByteArray hit = probeRoot(submoduleRoot, names);
    if (hit.isEmpty()) {
        const QString subCanon = submoduleRoot.isEmpty()
                                 ? QString()
                                 : QDir(submoduleRoot).canonicalPath();
        const QString wsCanon  = workspaceRoot.isEmpty()
                                 ? QString()
                                 : QDir(workspaceRoot).canonicalPath();
        if (!wsCanon.isEmpty() && wsCanon != subCanon) {
            hit = probeRoot(workspaceRoot, names);
        }
    }
    if (hit.isEmpty()) return {};

    const QByteArray bounded = truncateToBudget(hit, budgetBytes);
    return QString::fromUtf8(bounded).trimmed();
}

} // namespace ai
