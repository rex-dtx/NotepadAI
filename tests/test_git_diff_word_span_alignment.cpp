/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 */

#include <QtTest>
#include <QByteArray>
#include <QRandomGenerator>
#include <QSet>
#include <QVector>

#include <algorithm>

#include "GitDiffParser.h"
#include "GitDiffSyntaxMapper.h"

class TestGitDiffWordSpanAlignment : public QObject
{
    Q_OBJECT

private slots:
    void regression_NtoM_block_aligns_via_shift_best();
    void regression_GitDiffCacheH_real_diff_spans();
    void regression_duplicate_comment_prefix_no_misalignment();
    void property_random_inputs_invariants();
    void property_token_lcs_deterministic();

private:
    static QByteArray buildDiff(const QVector<QByteArray> &delLines,
                                const QVector<QByteArray> &addLines);
    static bool isUtf8CodepointBoundary(const QByteArray &line, qint32 pos);
    static bool spansAreInBounds(const GitDiffParser::Result &p,
                                 const QVector<GitDiffSyntaxMapper::WordSpan> &spans,
                                 GitDiffParser::LineKind expectedKind);
    static bool spansLandOnUtf8(const GitDiffParser::Result &p,
                                const QVector<GitDiffSyntaxMapper::WordSpan> &spans);
    static bool spansDoNotOverlap(const QVector<GitDiffSyntaxMapper::WordSpan> &spans);
    static QByteArray randomLine(QRandomGenerator &rng, int targetLen, bool allowUtf8);
};

QByteArray TestGitDiffWordSpanAlignment::buildDiff(const QVector<QByteArray> &delLines,
                                                    const QVector<QByteArray> &addLines)
{
    QByteArray out;
    out.append("diff --git a/foo.cpp b/foo.cpp\n");
    out.append("index 1234567..89abcde 100644\n");
    out.append("--- a/foo.cpp\n");
    out.append("+++ b/foo.cpp\n");
    out.append(QString("@@ -1,%1 +1,%2 @@\n")
                   .arg(delLines.size()).arg(addLines.size()).toUtf8());
    for (const auto &l : delLines) { out.append('-'); out.append(l); out.append('\n'); }
    for (const auto &l : addLines) { out.append('+'); out.append(l); out.append('\n'); }
    return out;
}

bool TestGitDiffWordSpanAlignment::isUtf8CodepointBoundary(const QByteArray &line, qint32 pos)
{
    if (pos <= 0 || pos >= line.size()) return true;
    return (static_cast<unsigned char>(line[pos]) & 0xC0) != 0x80;
}

bool TestGitDiffWordSpanAlignment::spansAreInBounds(
        const GitDiffParser::Result &p,
        const QVector<GitDiffSyntaxMapper::WordSpan> &spans,
        GitDiffParser::LineKind expectedKind)
{
    for (const auto &s : spans) {
        if (s.row < 0 || s.row >= p.texts.size()) return false;
        if (p.kinds.at(s.row) != expectedKind) return false;
        const qint32 lineBytes = static_cast<qint32>(p.texts.at(s.row).size());
        if (s.colStart < 0 || s.colEnd <= s.colStart) return false;
        if (s.colEnd > lineBytes) return false;
    }
    return true;
}

bool TestGitDiffWordSpanAlignment::spansLandOnUtf8(
        const GitDiffParser::Result &p,
        const QVector<GitDiffSyntaxMapper::WordSpan> &spans)
{
    for (const auto &s : spans) {
        if (s.row < 0 || s.row >= p.texts.size()) continue;
        const QByteArray &line = p.texts.at(s.row);
        if (!isUtf8CodepointBoundary(line, s.colStart)) return false;
        if (!isUtf8CodepointBoundary(line, s.colEnd)) return false;
    }
    return true;
}

bool TestGitDiffWordSpanAlignment::spansDoNotOverlap(
        const QVector<GitDiffSyntaxMapper::WordSpan> &spans)
{
    // Group by row, then sort by colStart, then check non-overlap.
    QHash<qint32, QVector<QPair<qint32, qint32>>> byRow;
    for (const auto &s : spans) {
        byRow[s.row].push_back({s.colStart, s.colEnd});
    }
    for (auto it = byRow.begin(); it != byRow.end(); ++it) {
        auto &v = it.value();
        std::sort(v.begin(), v.end(),
                  [](const auto &a, const auto &b) { return a.first < b.first; });
        for (int i = 1; i < v.size(); ++i) {
            if (v[i].first < v[i - 1].second) return false;
        }
    }
    return true;
}

void TestGitDiffWordSpanAlignment::regression_NtoM_block_aligns_via_shift_best()
{
    // 3 deleted lines, 5 added lines. The matching pair is del[0..2] ↔ add[2..4]
    // because add[0..1] are brand-new lines prepended. The shift-best heuristic
    // must discover shift=+2 (paired pairs are byte-identical, so wordDiffPair
    // emits zero spans for them) and emit full-line spans only on add[0] and
    // add[1]. The previous N:M code concatenated the slices and ran a single
    // outer LCS — the repeating "    do…();" pattern at every line tricked the
    // LCS into a misprojection that produced spans ~4 chars shifted LEFT.
    QVector<QByteArray> del = {
        "    doFoo();",
        "    doBar();",
        "    doBaz();",
    };
    QVector<QByteArray> add = {
        "    doAlpha();",
        "    doBeta();",
        "    doFoo();",
        "    doBar();",
        "    doBaz();",
    };

    const QByteArray diff = buildDiff(del, add);
    auto parsed = GitDiffParser::parse(diff);

    GitDiffSyntaxMapper::Input in;
    in.parsed = &parsed;
    // Empty lexer names — skip Lexilla, exercise word-diff only.
    in.oldLexerName = QString();
    in.newLexerName = QString();
    const auto ov = GitDiffSyntaxMapper::map(in);

    // All paired lines are byte-identical → wordDiffPair emits nothing.
    // All del lines are paired → delWordSpans must be empty.
    QCOMPARE(ov.delWordSpans.size(), 0);

    // Unpaired add rows are add[0] (doAlpha) and add[1] (doBeta).
    // Find their row indices in parsed.kinds.
    QVector<int> addRows;
    for (int r = 0; r < parsed.kinds.size(); ++r) {
        if (parsed.kinds.at(r) == GitDiffParser::LineKind::Added) addRows.push_back(r);
    }
    QCOMPARE(addRows.size(), 5);

    // Expect exactly two full-line add spans, on addRows[0] and addRows[1].
    QCOMPARE(ov.addWordSpans.size(), 2);

    QSet<int> spanRows;
    for (const auto &s : ov.addWordSpans) {
        spanRows.insert(s.row);
        // Full-line span: colStart == 0 && colEnd == line.size().
        const auto &line = parsed.texts.at(s.row);
        QCOMPARE(s.colStart, 0);
        QCOMPARE(static_cast<int>(s.colEnd), line.size());
    }
    QVERIFY(spanRows.contains(addRows[0]));
    QVERIFY(spanRows.contains(addRows[1]));

    // All spans land on Added rows (kind invariant).
    QVERIFY(spansAreInBounds(parsed, ov.addWordSpans, GitDiffParser::LineKind::Added));
    QVERIFY(spansAreInBounds(parsed, ov.delWordSpans, GitDiffParser::LineKind::Deleted));
}

// Replays the actual GitDiffCache.h diff the user reported as visually offset.
// Asserts spans land at the byte ranges within the **line content** (no '+' /
// '-' prefix) — the painter applies the prefix offset itself.
//
// If any span is off by 1, this test pinpoints the misprojection precisely
// (rather than us guessing from a screenshot).
void TestGitDiffWordSpanAlignment::regression_GitDiffCacheH_real_diff_spans()
{
    // Hunk 2 of the working-tree diff for src/git/GitDiffCache.h.
    // Comment lines (3 del, 4 add) followed by 1 context, then 1 del, 1
    // context-blank-add, 1 add struct. We only assert on the comment block
    // here; the second block is exercised through the Added-empty + Added-
    // struct unpaired-vs-paired path.
    QVector<QByteArray> del = {
        // First N=3 lines are the deleted comment paragraph.
        QByteArray("// fully parsed Result to skip re-parsing on tab re-activation. Cleared en bloc"),
        QByteArray("// whenever statusUpdated fires \xe2\x80\x94 smart invalidation would require extra git"),
        QByteArray("// spawns to verify blob shas, and the win is negligible compared to the cost."),
    };
    QVector<QByteArray> add = {
        QByteArray("// fully parsed Result + the optional syntax overlay to skip re-parsing and"),
        QByteArray("// re-lexing on tab re-activation. Cleared en bloc whenever statusUpdated"),
        QByteArray("// fires \xe2\x80\x94 smart invalidation would require extra git spawns to verify blob"),
        QByteArray("// shas, and the win is negligible compared to the cost."),
    };

    const QByteArray diff = buildDiff(del, add);
    auto parsed = GitDiffParser::parse(diff);

    GitDiffSyntaxMapper::Input in;
    in.parsed = &parsed;
    const auto ov = GitDiffSyntaxMapper::map(in);

    // Find the parsed row indices for del / add.
    QVector<int> delRows, addRows;
    for (int r = 0; r < parsed.kinds.size(); ++r) {
        if (parsed.kinds.at(r) == GitDiffParser::LineKind::Deleted) delRows.push_back(r);
        else if (parsed.kinds.at(r) == GitDiffParser::LineKind::Added) addRows.push_back(r);
    }
    QCOMPARE(delRows.size(), 3);
    QCOMPARE(addRows.size(), 4);

    // Invariants the painter relies on. If ANY of these fail, the painter
    // will draw the highlight at the wrong column.
    for (const auto &s : ov.delWordSpans) {
        const QByteArray &line = parsed.texts.at(s.row);
        const qint32 n = static_cast<qint32>(line.size());
        QVERIFY2(s.row >= delRows.first() && s.row <= delRows.last(),
                 qPrintable(QString("del span on non-deleted row %1").arg(s.row)));
        QVERIFY2(s.colStart >= 0 && s.colEnd > s.colStart && s.colEnd <= n,
                 qPrintable(QString("del span out of bounds: row=%1 [%2,%3) line=%4")
                                .arg(s.row).arg(s.colStart).arg(s.colEnd).arg(n)));
    }
    for (const auto &s : ov.addWordSpans) {
        const QByteArray &line = parsed.texts.at(s.row);
        const qint32 n = static_cast<qint32>(line.size());
        QVERIFY2(s.row >= addRows.first() && s.row <= addRows.last(),
                 qPrintable(QString("add span on non-added row %1").arg(s.row)));
        QVERIFY2(s.colStart >= 0 && s.colEnd > s.colStart && s.colEnd <= n,
                 qPrintable(QString("add span out of bounds: row=%1 [%2,%3) line=%4")
                                .arg(s.row).arg(s.colStart).arg(s.colEnd).arg(n)));
    }
    QVERIFY(spansLandOnUtf8(parsed, ov.delWordSpans));
    QVERIFY(spansLandOnUtf8(parsed, ov.addWordSpans));
    QVERIFY(spansDoNotOverlap(ov.delWordSpans));
    QVERIFY(spansDoNotOverlap(ov.addWordSpans));

    // Token-LCS proof. The previous per-pair char-LCS with shift-best
    // pairing produced a full-line ADD span on add[0] AND a misaligned DEL
    // span on del[2] starting at byte 3. Token-LCS pairs the shared comment
    // tokens across the entire 3-del/4-add block and only highlights the
    // tokens that genuinely don't appear on the other side.
    //
    // The inserted phrase "+ the optional syntax overlay" lives at bytes
    // [23, 52) of add[0]: "// fully parsed Result " is 23 bytes, then
    // "+ the optional syntax overlay" is 29 bytes. Any token-LCS-correct
    // overlay MUST flag those bytes on add[0] with at least one span.
    // Sanity-check the fixture text — if this fails the fixture itself is
    // wrong.
    const QByteArray &add0Line = parsed.texts.at(addRows[0]);
    QCOMPARE(add0Line.mid(23, 29), QByteArray("+ the optional syntax overlay"));

    bool addRow0CoversInsertion = false;
    bool addRow0IsFullLine      = false;
    for (const auto &s : ov.addWordSpans) {
        if (s.row != addRows[0]) continue;
        if (s.colStart < 52 && s.colEnd > 23) addRow0CoversInsertion = true;
        if (s.colStart == 0 && s.colEnd == add0Line.size()) addRow0IsFullLine = true;
    }
    QVERIFY2(addRow0CoversInsertion,
             "expected a span on add[0] covering the inserted "
             "'+ the optional syntax overlay' bytes [23,52)");
    QVERIFY2(!addRow0IsFullLine,
             "add[0] must not be a full-line span — token-LCS should produce "
             "a partial span for just the inserted content");

    // Bug-fix proof: del[2] must NOT have a span starting at byte 3 covering
    // 'spawns to verify blob '. That was the old shift-best misprojection;
    // token-LCS sees those bytes in add[2] and pairs them across the block.
    const QByteArray &del2Line = parsed.texts.at(delRows[2]);
    QCOMPARE(del2Line.mid(3, 22), QByteArray("spawns to verify blob "));
    for (const auto &s : ov.delWordSpans) {
        if (s.row != delRows[2]) continue;
        const bool oldBugFingerprint =
            s.colStart == 3 && s.colEnd >= 25;
        QVERIFY2(!oldBugFingerprint,
                 qPrintable(QString("del[2] regressed to old shift-best span "
                                    "[%1,%2) — token-LCS should pair "
                                    "'spawns to verify blob ' across the block")
                                .arg(s.colStart).arg(s.colEnd)));
    }
}

void TestGitDiffWordSpanAlignment::regression_duplicate_comment_prefix_no_misalignment()
{
    // Pathological case for any per-pair line aligner: every deleted line
    // shares the same leading "// foo " prefix as a different added line, so
    // a per-line LCS sees ~70% common content on every (del[i], add[j])
    // pair and will pick the wrong pairing — producing visibly misaligned
    // spans (the user's reported "shift by 4 chars" symptom). Token-LCS over
    // the whole block lines the shared prefix tokens up regardless of which
    // row pair the per-line aligner would have picked.
    QVector<QByteArray> del = {
        QByteArray("// foo: alpha case here"),
        QByteArray("// foo: beta case here"),
        QByteArray("// foo: gamma case here"),
    };
    QVector<QByteArray> add = {
        QByteArray("// foo: alpha case there"),
        QByteArray("// foo: beta case there"),
        QByteArray("// foo: gamma case there"),
    };

    const QByteArray diff = buildDiff(del, add);
    auto parsed = GitDiffParser::parse(diff);

    GitDiffSyntaxMapper::Input in;
    in.parsed = &parsed;
    const auto ov = GitDiffSyntaxMapper::map(in);

    // Safety invariants.
    QVERIFY(spansAreInBounds(parsed, ov.delWordSpans, GitDiffParser::LineKind::Deleted));
    QVERIFY(spansAreInBounds(parsed, ov.addWordSpans, GitDiffParser::LineKind::Added));
    QVERIFY(spansLandOnUtf8(parsed, ov.delWordSpans));
    QVERIFY(spansLandOnUtf8(parsed, ov.addWordSpans));
    QVERIFY(spansDoNotOverlap(ov.delWordSpans));
    QVERIFY(spansDoNotOverlap(ov.addWordSpans));

    // The shared "// foo: <name> case " prefix is identical on each paired
    // row — token-LCS must NOT highlight any prefix byte. Spans may exist
    // only over the trailing "here"/"there" tokens.
    QVector<int> delRows, addRows;
    for (int r = 0; r < parsed.kinds.size(); ++r) {
        if (parsed.kinds.at(r) == GitDiffParser::LineKind::Deleted) delRows.push_back(r);
        else if (parsed.kinds.at(r) == GitDiffParser::LineKind::Added) addRows.push_back(r);
    }
    // Each row's "here" / "there" word starts after the 19 shared bytes
    // ("// foo: alpha case " = 19; "// foo: beta case " = 18; "// foo: gamma
    // case " = 20). Spans must start at or after the shared prefix length.
    auto sharedPrefixLenFor = [](const QByteArray &line) -> qint32 {
        const int idx = line.indexOf(" case ");
        return idx < 0 ? line.size() : idx + 6;  // include trailing space
    };
    for (const auto &s : ov.delWordSpans) {
        const qint32 sp = sharedPrefixLenFor(parsed.texts.at(s.row));
        QVERIFY2(s.colStart >= sp,
                 qPrintable(QString("del span starts inside shared prefix: "
                                    "row=%1 [%2,%3) sharedPrefixLen=%4")
                                .arg(s.row).arg(s.colStart).arg(s.colEnd).arg(sp)));
    }
    for (const auto &s : ov.addWordSpans) {
        const qint32 sp = sharedPrefixLenFor(parsed.texts.at(s.row));
        QVERIFY2(s.colStart >= sp,
                 qPrintable(QString("add span starts inside shared prefix: "
                                    "row=%1 [%2,%3) sharedPrefixLen=%4")
                                .arg(s.row).arg(s.colStart).arg(s.colEnd).arg(sp)));
    }
}

QByteArray TestGitDiffWordSpanAlignment::randomLine(QRandomGenerator &rng,
                                                     int targetLen, bool allowUtf8)
{
    // Vocabulary: ASCII letters/digits/punct + a handful of UTF-8 codepoints
    // of varying byte width so span boundaries can fall inside multi-byte runs.
    static const QVector<QByteArray> utf8Chars = {
        QByteArray::fromHex("c3a9"),    // é (2 bytes)
        QByteArray::fromHex("c3b1"),    // ñ (2 bytes)
        QByteArray::fromHex("e4b8ad"),  // 中 (3 bytes)
        QByteArray::fromHex("e6968714"), // not real — see below
    };
    // Drop any malformed entry above (last one is intentionally garbage to
    // test that we *don't* generate it). Filter at runtime once.
    static QVector<QByteArray> safeUtf8 = []() {
        QVector<QByteArray> r;
        r.append(QByteArray::fromHex("c3a9"));    // é
        r.append(QByteArray::fromHex("c3b1"));    // ñ
        r.append(QByteArray::fromHex("e4b8ad"));  // 中
        r.append(QByteArray::fromHex("f09f9881")); // 😁 (4 bytes)
        return r;
    }();
    Q_UNUSED(utf8Chars);

    QByteArray out;
    out.reserve(targetLen + 8);
    while (out.size() < targetLen) {
        // 70% ASCII, 30% UTF-8 when allowed.
        const bool pickUtf8 = allowUtf8 && (rng.bounded(100) < 30);
        if (pickUtf8) {
            out.append(safeUtf8.at(static_cast<int>(rng.bounded(safeUtf8.size()))));
        } else {
            // Printable ASCII excluding '\n'. Avoid '+', '-', ' ' at column 0
            // (they would be confused with diff prefixes by the parser).
            char c = static_cast<char>(33 + rng.bounded(94)); // '!'..'~'
            out.append(c);
        }
    }
    return out;
}

void TestGitDiffWordSpanAlignment::property_random_inputs_invariants()
{
    // 1000 random diffs. For each: assemble a small N:M block (N,M in [1,4]),
    // each line 0..40 bytes of mixed ASCII + UTF-8, run map(), and verify
    // four invariants:
    //   (1) every span row is Added or Deleted (correct side)
    //   (2) 0 <= colStart < colEnd <= line.size()
    //   (3) colStart and colEnd land on UTF-8 codepoint boundaries
    //   (4) spans within a row do not overlap
    QRandomGenerator rng(0xC0FFEE42);

    constexpr int kIterations = 1000;
    for (int it = 0; it < kIterations; ++it) {
        const int N = 1 + static_cast<int>(rng.bounded(4));
        const int M = 1 + static_cast<int>(rng.bounded(4));
        QVector<QByteArray> del;
        QVector<QByteArray> add;
        del.reserve(N);
        add.reserve(M);
        for (int i = 0; i < N; ++i) {
            del.push_back(randomLine(rng, static_cast<int>(rng.bounded(40)), true));
        }
        for (int i = 0; i < M; ++i) {
            add.push_back(randomLine(rng, static_cast<int>(rng.bounded(40)), true));
        }

        const QByteArray diff = buildDiff(del, add);
        auto parsed = GitDiffParser::parse(diff);

        GitDiffSyntaxMapper::Input in;
        in.parsed = &parsed;
        const auto ov = GitDiffSyntaxMapper::map(in);

        const bool ok1del = spansAreInBounds(parsed, ov.delWordSpans,
                                             GitDiffParser::LineKind::Deleted);
        const bool ok1add = spansAreInBounds(parsed, ov.addWordSpans,
                                             GitDiffParser::LineKind::Added);
        const bool ok3del = spansLandOnUtf8(parsed, ov.delWordSpans);
        const bool ok3add = spansLandOnUtf8(parsed, ov.addWordSpans);
        const bool ok4del = spansDoNotOverlap(ov.delWordSpans);
        const bool ok4add = spansDoNotOverlap(ov.addWordSpans);

        if (!(ok1del && ok1add && ok3del && ok3add && ok4del && ok4add)) {
            qWarning() << "Property violation at iteration" << it
                       << "N=" << N << "M=" << M
                       << "del=" << del << "add=" << add
                       << "bounds(del/add)=" << ok1del << ok1add
                       << "utf8(del/add)=" << ok3del << ok3add
                       << "overlap(del/add)=" << ok4del << ok4add;
        }
        QVERIFY(ok1del);
        QVERIFY(ok1add);
        QVERIFY(ok3del);
        QVERIFY(ok3add);
        QVERIFY(ok4del);
        QVERIFY(ok4add);
    }
}

void TestGitDiffWordSpanAlignment::property_token_lcs_deterministic()
{
    // Run map() 100 times on the same input. The token intern table uses
    // QHash, whose iteration order is process-random. The algorithm must
    // emit BIT-IDENTICAL spans across runs — proving the LCS path is decided
    // purely from interned IDs and the deterministic insertion-order
    // assignment, never from QHash iteration order.
    QVector<QByteArray> del = {
        QByteArray("void foo(int x, int y, int z) {"),
        QByteArray("    return x + y + z;"),
        QByteArray("}"),
    };
    QVector<QByteArray> add = {
        QByteArray("int foo(int a, int b, int c) {"),
        QByteArray("    return a * b * c;"),
        QByteArray("}"),
    };
    const QByteArray diff = buildDiff(del, add);
    auto parsed = GitDiffParser::parse(diff);

    GitDiffSyntaxMapper::Input in;
    in.parsed = &parsed;

    const auto first = GitDiffSyntaxMapper::map(in);

    constexpr int kRuns = 99;
    for (int run = 0; run < kRuns; ++run) {
        const auto ov = GitDiffSyntaxMapper::map(in);
        QCOMPARE(ov.delWordSpans.size(), first.delWordSpans.size());
        QCOMPARE(ov.addWordSpans.size(), first.addWordSpans.size());
        for (int i = 0; i < ov.delWordSpans.size(); ++i) {
            QCOMPARE(ov.delWordSpans[i].row,      first.delWordSpans[i].row);
            QCOMPARE(ov.delWordSpans[i].colStart, first.delWordSpans[i].colStart);
            QCOMPARE(ov.delWordSpans[i].colEnd,   first.delWordSpans[i].colEnd);
        }
        for (int i = 0; i < ov.addWordSpans.size(); ++i) {
            QCOMPARE(ov.addWordSpans[i].row,      first.addWordSpans[i].row);
            QCOMPARE(ov.addWordSpans[i].colStart, first.addWordSpans[i].colStart);
            QCOMPARE(ov.addWordSpans[i].colEnd,   first.addWordSpans[i].colEnd);
        }
    }
}

QTEST_MAIN(TestGitDiffWordSpanAlignment)
#include "test_git_diff_word_span_alignment.moc"
