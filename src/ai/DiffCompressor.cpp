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

#include "DiffCompressor.h"

#include <QList>

namespace ai {

namespace {

constexpr auto kTruncMarker = " \xE2\x80\xA6[truncated]";   // " …[truncated]"
constexpr auto kSkippedFmt  = "@@ ... %1 hunks omitted ...\n";

// Walk back to last UTF-8 character boundary at or below `maxBytes`.
int findUtf8Boundary(const QByteArray &line, int maxBytes)
{
    if (maxBytes >= line.size()) return line.size();
    if (maxBytes <= 0) return 0;
    int i = maxBytes;
    // Continuation byte: 0b10xxxxxx == 0x80..0xBF. Walk back until we land
    // on a byte that is NOT a continuation byte (which is either a start of
    // a multi-byte sequence or an ASCII byte).
    while (i > 0 && (static_cast<unsigned char>(line.at(i)) & 0xC0) == 0x80) {
        --i;
    }
    return i;
}

// One hunk in the parsed diff: bytes (including the trailing newline) and
// whether it starts with the `@@ ... @@` hunk header (vs being a file header
// block, which precedes hunks).
struct Hunk {
    QByteArray bytes;
};

struct FileGroup {
    QByteArray headerBytes;       // file header lines (--- / +++ / index / etc.)
    QList<Hunk> hunks;            // each "@@ ... @@\n<changes>" block
    QByteArray filePath;          // for deterministic tiebreak
    int droppedCount = 0;
};

// Parse a unified diff into per-file groups. We split on lines starting with
// "diff --git " (the canonical file separator emitted by `git diff`). For each
// file, hunks start with "@@ ". Anything between the diff-header and the first
// "@@" is the file header (mode lines, index lines, ---/+++ lines).
QList<FileGroup> parseGroups(const QByteArray &input)
{
    QList<FileGroup> groups;
    const auto lines = input.split('\n');

    FileGroup current;
    bool inFile = false;
    bool inHunk = false;

    auto flushHunk = [&](QByteArray &hunkAccum) {
        if (!hunkAccum.isEmpty()) {
            current.hunks.append(Hunk{hunkAccum});
            hunkAccum.clear();
        }
    };

    QByteArray hunkAccum;

    for (int idx = 0; idx < lines.size(); ++idx) {
        QByteArray line = lines.at(idx);
        // Re-append the trailing '\n' that split() consumed, except for the
        // final synthetic empty trailing element produced when input ends in '\n'.
        const bool isLastSplitFragment = (idx == lines.size() - 1);
        if (!isLastSplitFragment) line.append('\n');
        else if (line.isEmpty()) continue;   // skip trailing empty fragment

        if (line.startsWith("diff --git ")) {
            // Flush previous file.
            if (inHunk) flushHunk(hunkAccum);
            if (inFile) groups.append(std::move(current));
            current = FileGroup{};
            current.headerBytes = line;
            // Extract path from "diff --git a/path b/path". Best-effort; used
            // only for deterministic tiebreak ordering.
            const int aPos = line.indexOf(" a/");
            const int bPos = line.indexOf(" b/", aPos < 0 ? 0 : aPos + 1);
            if (aPos > 0 && bPos > aPos) {
                current.filePath = line.mid(aPos + 3, bPos - aPos - 3);
            }
            inFile = true;
            inHunk = false;
        } else if (line.startsWith("@@ ")) {
            if (inHunk) flushHunk(hunkAccum);
            hunkAccum = line;
            inHunk = true;
        } else {
            if (inHunk) {
                hunkAccum.append(line);
            } else if (inFile) {
                current.headerBytes.append(line);
            } else {
                // Leading bytes before any "diff --git" — synthesize a single
                // catch-all group so we never silently lose content.
                current.headerBytes.append(line);
                inFile = true;
            }
        }
    }
    if (inHunk) flushHunk(hunkAccum);
    if (inFile) groups.append(std::move(current));
    return groups;
}

QByteArray serializeGroups(const QList<FileGroup> &groups)
{
    QByteArray out;
    // Reserve cheaply to avoid repeated allocation.
    int total = 0;
    for (const auto &g : groups) {
        total += g.headerBytes.size();
        for (const auto &h : g.hunks) total += h.bytes.size();
        if (g.droppedCount > 0) total += 48;
    }
    out.reserve(total);

    for (const auto &g : groups) {
        out.append(g.headerBytes);
        for (const auto &h : g.hunks) out.append(h.bytes);
        if (g.droppedCount > 0) {
            out.append(QByteArray::fromStdString(
                std::string("@@ ... ") + std::to_string(g.droppedCount)
                + " hunks omitted ...\n"));
        }
    }
    return out;
}

QByteArray pass2TruncateLines(const QByteArray &input)
{
    if (input.isEmpty()) return input;
    QByteArray out;
    out.reserve(input.size());

    int start = 0;
    while (start < input.size()) {
        int nl = input.indexOf('\n', start);
        if (nl < 0) nl = input.size() - 1;       // last line without trailing \n
        const int lineLen = nl - start + 1;
        if (lineLen <= DiffCompressor::kLineThresholdBytes) {
            out.append(input.constData() + start, lineLen);
        } else {
            // Truncate. We aim for kLineThreshold bytes total INCLUDING the
            // marker + trailing newline, walking back to a UTF-8 boundary.
            const int markerLen = int(qstrlen(kTruncMarker));
            const int target = DiffCompressor::kLineThresholdBytes - markerLen - 1;
            const QByteArray lineView = QByteArray::fromRawData(
                input.constData() + start,
                (input.at(nl) == '\n') ? lineLen - 1 : lineLen);
            const int cut = findUtf8Boundary(lineView, qMax(0, target));
            out.append(lineView.constData(), cut);
            out.append(kTruncMarker);
            out.append('\n');
        }
        start = nl + 1;
    }
    return out;
}

} // namespace

int DiffCompressor::utf8BoundaryAtOrBelow(const QByteArray &line, int maxBytes)
{
    return findUtf8Boundary(line, maxBytes);
}

QByteArray DiffCompressor::compress(const QByteArray &unifiedDiff, int budgetBytes)
{
    // Pass 1: pass-through.
    if (unifiedDiff.size() <= budgetBytes) return unifiedDiff;

    // Pass 2: per-line truncation.
    QByteArray truncated = pass2TruncateLines(unifiedDiff);
    if (truncated.size() <= budgetBytes) return truncated;

    // Pass 3: parse, drop hunks from file with most remaining hunks.
    QList<FileGroup> groups = parseGroups(truncated);
    if (groups.isEmpty()) return truncated;   // nothing to do — return Pass 2 output

    auto canDropMore = [&]() {
        for (const auto &g : groups) if (g.hunks.size() >= 2) return true;
        return false;
    };

    while (serializeGroups(groups).size() > budgetBytes && canDropMore()) {
        // Pick target: max hunks. Tiebreak: lex order on filePath.
        int targetIdx = -1;
        for (int i = 0; i < groups.size(); ++i) {
            if (groups[i].hunks.size() < 2) continue;
            if (targetIdx < 0) { targetIdx = i; continue; }
            const auto &cur = groups[targetIdx];
            const auto &cand = groups[i];
            if (cand.hunks.size() > cur.hunks.size()) {
                targetIdx = i;
            } else if (cand.hunks.size() == cur.hunks.size()
                       && cand.filePath < cur.filePath) {
                targetIdx = i;
            }
        }
        if (targetIdx < 0) break;
        groups[targetIdx].hunks.removeLast();   // drop last (closest-to-EOF) hunk
        groups[targetIdx].droppedCount += 1;
    }

    // Pass 4: irreducible — return what we have, even if still over budget.
    return serializeGroups(groups);
}

} // namespace ai
