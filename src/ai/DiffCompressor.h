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

#ifndef AI_DIFF_COMPRESSOR_H
#define AI_DIFF_COMPRESSOR_H

#include <QByteArray>

namespace ai {

// Deterministic 4-pass diff compressor with bounded byte budget.
//
//   Pass 1 — pass-through if input ≤ budget.
//   Pass 2 — per-line truncation at UTF-8 char boundary, threshold 256 bytes.
//   Pass 3 — iterative hunk drop: each iter drops one hunk from the file with
//            the MOST remaining hunks (≥ 2). Tiebreak: lex file-path order.
//            Invariant: at least one hunk per file retained.
//   Pass 4 — irreducible best-effort. Return as-is (may exceed budget).
//
// Pure function; no I/O, no global state. UTF-8 safe: never produces invalid
// UTF-8 even on multi-byte char straddling the per-line threshold.
class DiffCompressor
{
public:
    static constexpr int kDefaultBudgetBytes = 20'000;
    static constexpr int kLineThresholdBytes = 256;

    // Compress `unified_diff` to within `budget_bytes` using the algorithm
    // described above. If input already fits, returned bytes equal input.
    static QByteArray compress(const QByteArray &unifiedDiff, int budgetBytes = kDefaultBudgetBytes);

    // Pass 2 exposed for unit testing: walk back to last UTF-8 char boundary
    // at or below maxBytes. Result is always a valid prefix in UTF-8 terms.
    // Returns the byte index (0..line.size()) one past the last kept byte.
    static int utf8BoundaryAtOrBelow(const QByteArray &line, int maxBytes);
};

} // namespace ai

#endif // AI_DIFF_COMPRESSOR_H
