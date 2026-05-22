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

#ifndef GIT_DIFF_PAINTER_H
#define GIT_DIFF_PAINTER_H

#include "GitDiffParser.h"

#include <cstdint>

class ScintillaNext;
struct GitDiffPalette;

// Owns the rendering of a parsed unified diff into a ScintillaNext editor.
// One-shot append + style flush — no incremental painting, no per-line style
// calls. Always issues SCI_SETREADONLY(true) at the end.
class GitDiffPainter
{
public:
    // Style IDs we own.
    enum StyleId : std::uint8_t {
        StyleDefault    = 0,
        StyleFileHeader = 1,
        StyleHunkHeader = 2,
        StyleContext    = 3,
        StyleAdded      = 4,
        StyleDeleted    = 5,
        StyleNoNewline  = 6
    };

    // Configure the editor's styling (lexer off, our color palette installed).
    // Idempotent — safe to call multiple times.
    static void configureEditor(ScintillaNext *editor, const GitDiffPalette &pal);

    // Renders the parsed result. Clears the buffer first. Leaves the editor
    // in read-only mode.
    static void render(ScintillaNext *editor, const GitDiffParser::Result &parsed);
};

#endif // GIT_DIFF_PAINTER_H
