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

#ifndef AI_PROMPT_ASSEMBLER_H
#define AI_PROMPT_ASSEMBLER_H

#include <QByteArray>
#include <QString>

namespace ai {

// Assembles the prompt sent to the LLM by substituting three placeholders in
// the user-configured template:
//   {{rules}}        — project-rules block (empty if no rules file)
//   {{subject_hint}} — user-provided subject line (empty if none)
//   {{diff}}         — the compressed unified diff
//
// After substitution, runs of 3+ newlines collapse to 2 so empty sections do
// not leave stray blank gaps.
//
// Pure function; no I/O. The DEFAULT_TEMPLATE produces Conventional Commits
// shaped output and instructs the model to emit body-only when a subject hint
// is present.
class PromptAssembler
{
public:
    static QString defaultTemplate();

    // Render the rules block. Returns empty string if rulesContent is empty
    // (after trim), else a section like "Project rules (follow these):\n...".
    static QString renderRulesBlock(const QString &rulesContent);

    // Render the subject-hint block. Empty if subjectLine is empty (after
    // trim), else a section instructing the model to emit body-only.
    static QString renderSubjectHintBlock(const QString &subjectLine);

    // Render the diff block. Always includes the diff bytes (UTF-8 decoded).
    static QString renderDiffBlock(const QByteArray &compressedDiff);

    // Substitute placeholders and collapse extra blank lines. Unknown
    // placeholders are left untouched.
    static QString assemble(const QString &templateBody,
                            const QString &rulesContent,
                            const QString &subjectLine,
                            const QByteArray &compressedDiff);
};

} // namespace ai

#endif // AI_PROMPT_ASSEMBLER_H
