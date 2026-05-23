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

#ifndef AI_RULES_LOCATOR_H
#define AI_RULES_LOCATOR_H

#include <QString>
#include <QStringList>

namespace ai {

// Look up the project rules file at a repository root and return its content
// (truncated to a byte budget at the last line boundary).
//
// Precedence per root: CLAUDE.md → AGENTS.md → .rules (first non-empty wins).
//
// Two roots are tried in order: submoduleRoot first, then workspaceRoot. The
// two are NOT merged — first match anywhere wins. When the two roots resolve
// to the same canonical path, only one set of probes runs.
//
// Pure-ish: reads file content from disk (no other side effects). Returns
// empty string when no rules file is found or all matches are empty after
// trim().
class RulesLocator
{
public:
    static constexpr int kDefaultRulesByteBudget = 4'000;

    // Names probed at each root, in precedence order.
    static QStringList probeNames();

    // Read file at absolute `path`; return its raw content, or empty bytes
    // on read failure. Defined here to allow tests to call it directly.
    static QByteArray readIfExists(const QString &path);

    // Truncate content at the last \n boundary at or below `budgetBytes`,
    // appending a "[project rules truncated]" marker line. Content shorter
    // than the budget is returned unchanged. Pure.
    static QByteArray truncateToBudget(const QByteArray &content,
                                       int budgetBytes = kDefaultRulesByteBudget);

    // Resolve rules content for the given roots. submoduleRoot may equal
    // workspaceRoot (single-repo case) — the duplicate probe set is skipped.
    // Returns trimmed, budget-clamped content; empty string if no match.
    static QString locate(const QString &submoduleRoot,
                          const QString &workspaceRoot,
                          int budgetBytes = kDefaultRulesByteBudget);
};

} // namespace ai

#endif // AI_RULES_LOCATOR_H
