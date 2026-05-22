/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <QtTest>
#include <QElapsedTimer>

#include "GitDiffParser.h"

class TestGitDiffParser : public QObject
{
    Q_OBJECT

private slots:
    void empty_returnsEmpty();
    void singleHunk_textAndStats();
    void crlf_normalizedOut();
    void binaryMarker_setsFlag();
    void noNewlineMarker_classified();
    void multipleFiles_allFileHeadersCounted();
    void perf_5kLineDiff_under60ms();
};

void TestGitDiffParser::empty_returnsEmpty()
{
    auto r = GitDiffParser::parse(QByteArray());
    QCOMPARE(r.texts.size(), 0);
    QCOMPARE(r.addedCount, 0);
    QCOMPARE(r.deletedCount, 0);
    QVERIFY(!r.isBinary);
}

void TestGitDiffParser::singleHunk_textAndStats()
{
    const QByteArray diff =
        "diff --git a/foo.cpp b/foo.cpp\n"
        "index 1234567..89abcde 100644\n"
        "--- a/foo.cpp\n"
        "+++ b/foo.cpp\n"
        "@@ -1,3 +1,4 @@\n"
        " keep1\n"
        "-removed\n"
        "+added1\n"
        "+added2\n"
        " keep2\n";
    auto r = GitDiffParser::parse(diff);
    QCOMPARE(r.addedCount, 2);
    QCOMPARE(r.deletedCount, 1);
    QVERIFY(!r.isBinary);
    QVERIFY(r.texts.size() >= 8); // headers + 5 body lines
    QCOMPARE(r.texts.size(), r.kinds.size());
    QCOMPARE(r.texts.size(), r.oldLn.size());
    QCOMPARE(r.texts.size(), r.newLn.size());
}

void TestGitDiffParser::crlf_normalizedOut()
{
    const QByteArray diff =
        "@@ -1,1 +1,2 @@\r\n"
        " keep\r\n"
        "+new\r\n";
    auto r = GitDiffParser::parse(diff);
    QCOMPARE(r.addedCount, 1);
    // The text fields should not contain the trailing CR.
    for (const QByteArray &t : r.texts) {
        QVERIFY2(!t.endsWith('\r'),
                 ("CR survived in payload: " + t).constData());
    }
}

void TestGitDiffParser::binaryMarker_setsFlag()
{
    const QByteArray diff =
        "diff --git a/img.png b/img.png\n"
        "Binary files a/img.png and b/img.png differ\n";
    auto r = GitDiffParser::parse(diff);
    QVERIFY(r.isBinary);
}

void TestGitDiffParser::noNewlineMarker_classified()
{
    const QByteArray diff =
        "@@ -1,1 +1,1 @@\n"
        "-old\n"
        "\\ No newline at end of file\n"
        "+new\n";
    auto r = GitDiffParser::parse(diff);
    bool sawNoNewline = false;
    for (auto k : r.kinds) {
        if (k == GitDiffParser::LineKind::NoNewline) {
            sawNoNewline = true;
            break;
        }
    }
    QVERIFY(sawNoNewline);
}

void TestGitDiffParser::multipleFiles_allFileHeadersCounted()
{
    const QByteArray diff =
        "diff --git a/a.cpp b/a.cpp\n"
        "--- a/a.cpp\n"
        "+++ b/a.cpp\n"
        "@@ -1,1 +1,1 @@\n"
        "-x\n"
        "+y\n"
        "diff --git a/b.cpp b/b.cpp\n"
        "--- a/b.cpp\n"
        "+++ b/b.cpp\n"
        "@@ -1,1 +1,1 @@\n"
        "-p\n"
        "+q\n";
    auto r = GitDiffParser::parse(diff);
    QCOMPARE(r.addedCount, 2);
    QCOMPARE(r.deletedCount, 2);
    int hunkHeaders = 0;
    for (auto k : r.kinds) {
        if (k == GitDiffParser::LineKind::HunkHeader) ++hunkHeaders;
    }
    QCOMPARE(hunkHeaders, 2);
}

void TestGitDiffParser::perf_5kLineDiff_under60ms()
{
    // Build a synthetic 5k-line unified diff: alternating + / - / context lines
    // across one large hunk, with realistic line widths.
    QByteArray diff;
    diff.reserve(5000 * 80);
    diff.append("diff --git a/big.txt b/big.txt\n");
    diff.append("--- a/big.txt\n");
    diff.append("+++ b/big.txt\n");
    diff.append("@@ -1,5000 +1,5000 @@\n");
    const char *filler = "abcdefghij abcdefghij abcdefghij abcdefghij abcdefghij abcdefghij";
    for (int i = 0; i < 5000; ++i) {
        char prefix;
        switch (i % 3) {
            case 0: prefix = ' '; break;
            case 1: prefix = '+'; break;
            default: prefix = '-'; break;
        }
        diff.append(prefix);
        diff.append(filler);
        diff.append('\n');
    }

    // Warm up branch predictor / cache.
    (void)GitDiffParser::parse(diff);

    QElapsedTimer t;
    qint64 best = std::numeric_limits<qint64>::max();
    for (int run = 0; run < 3; ++run) {
        t.start();
        auto r = GitDiffParser::parse(diff);
        const qint64 elapsed = t.nsecsElapsed();
        best = std::min(best, elapsed);
        // Sanity: the parsed result must contain all source lines.
        QVERIFY(r.texts.size() >= 5000);
    }

    const double ms = best / 1.0e6;
    qInfo() << "GitDiffParser 5k-line best:" << ms << "ms";
    // Hard budget: parser must finish well under 60ms so the full
    // parse+paint+focus chain stays under the user-perceived threshold.
    QVERIFY2(ms < 60.0,
             qPrintable(QString("Parser too slow: %1ms (>60ms)").arg(ms)));
}

QTEST_GUILESS_MAIN(TestGitDiffParser)
#include "test_git_diff_parser.moc"
