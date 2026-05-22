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

#include "GitNumstatParser.h"

class TestGitNumstatParser : public QObject
{
    Q_OBJECT

private slots:
    void empty_returnsEmpty();
    void normalRecord_parsedCounts();
    void multipleRecords_allInserted();
    void binaryRecord_setsFlag();
    void renamedRecord_keyedByNewPath();
    void utf8Path_preserved();
    void mixedRecords_normalRenamedBinary();
    void truncatedInput_doesNotCrash();
};

void TestGitNumstatParser::empty_returnsEmpty()
{
    QHash<QString, GitNumstatParser::Stat> r = GitNumstatParser::parse(QByteArray());
    QCOMPARE(r.size(), 0);
}

void TestGitNumstatParser::normalRecord_parsedCounts()
{
    // added=12, deleted=3, path=foo/bar.cpp
    QByteArray in;
    in.append("12\t3\tfoo/bar.cpp");
    in.append('\0');
    auto r = GitNumstatParser::parse(in);
    QCOMPARE(r.size(), 1);
    QVERIFY(r.contains(QStringLiteral("foo/bar.cpp")));
    const auto s = r.value(QStringLiteral("foo/bar.cpp"));
    QCOMPARE(s.added, 12);
    QCOMPARE(s.deleted, 3);
    QVERIFY(!s.isBinary);
}

void TestGitNumstatParser::multipleRecords_allInserted()
{
    QByteArray in;
    in.append("5\t2\ta.cpp");
    in.append('\0');
    in.append("0\t10\tb.cpp");
    in.append('\0');
    in.append("100\t0\tdir/c.cpp");
    in.append('\0');
    auto r = GitNumstatParser::parse(in);
    QCOMPARE(r.size(), 3);
    QCOMPARE(r.value("a.cpp").added, 5);
    QCOMPARE(r.value("a.cpp").deleted, 2);
    QCOMPARE(r.value("b.cpp").added, 0);
    QCOMPARE(r.value("b.cpp").deleted, 10);
    QCOMPARE(r.value("dir/c.cpp").added, 100);
    QCOMPARE(r.value("dir/c.cpp").deleted, 0);
}

void TestGitNumstatParser::binaryRecord_setsFlag()
{
    QByteArray in;
    in.append("-\t-\tassets/logo.png");
    in.append('\0');
    auto r = GitNumstatParser::parse(in);
    QCOMPARE(r.size(), 1);
    const auto s = r.value(QStringLiteral("assets/logo.png"));
    QVERIFY(s.isBinary);
    QCOMPARE(s.added, -1);
    QCOMPARE(s.deleted, -1);
}

void TestGitNumstatParser::renamedRecord_keyedByNewPath()
{
    // Format: "added\tdeleted\t\0orig\0new\0"
    QByteArray in;
    in.append("7\t1\t");
    in.append('\0');
    in.append("old/path.cpp");
    in.append('\0');
    in.append("new/path.cpp");
    in.append('\0');
    auto r = GitNumstatParser::parse(in);
    QCOMPARE(r.size(), 1);
    // Renamed records are keyed by the *new* path.
    QVERIFY(r.contains(QStringLiteral("new/path.cpp")));
    QVERIFY(!r.contains(QStringLiteral("old/path.cpp")));
    const auto s = r.value(QStringLiteral("new/path.cpp"));
    QCOMPARE(s.added, 7);
    QCOMPARE(s.deleted, 1);
    QVERIFY(!s.isBinary);
}

void TestGitNumstatParser::utf8Path_preserved()
{
    const QString viPath = QStringLiteral("tài_liệu/báo_cáo.md");
    QByteArray in;
    in.append("3\t4\t");
    in.append(viPath.toUtf8());
    in.append('\0');
    auto r = GitNumstatParser::parse(in);
    QCOMPARE(r.size(), 1);
    QVERIFY(r.contains(viPath));
    QCOMPARE(r.value(viPath).added, 3);
    QCOMPARE(r.value(viPath).deleted, 4);
}

void TestGitNumstatParser::mixedRecords_normalRenamedBinary()
{
    QByteArray in;
    // Normal
    in.append("10\t2\tsrc/main.cpp");
    in.append('\0');
    // Renamed
    in.append("0\t0\t");
    in.append('\0');
    in.append("src/old.h");
    in.append('\0');
    in.append("src/new.h");
    in.append('\0');
    // Binary
    in.append("-\t-\tdoc/diagram.png");
    in.append('\0');
    // Normal again after a renamed (verifies state recovery)
    in.append("1\t1\tREADME.md");
    in.append('\0');

    auto r = GitNumstatParser::parse(in);
    QCOMPARE(r.size(), 4);

    QVERIFY(r.contains("src/main.cpp"));
    QCOMPARE(r.value("src/main.cpp").added, 10);
    QCOMPARE(r.value("src/main.cpp").deleted, 2);

    QVERIFY(r.contains("src/new.h"));
    QVERIFY(!r.contains("src/old.h"));

    QVERIFY(r.contains("doc/diagram.png"));
    QVERIFY(r.value("doc/diagram.png").isBinary);

    QVERIFY(r.contains("README.md"));
    QCOMPARE(r.value("README.md").added, 1);
    QCOMPARE(r.value("README.md").deleted, 1);
}

void TestGitNumstatParser::truncatedInput_doesNotCrash()
{
    // Missing trailing nul + missing second tab variants — parser must just stop
    // without dereferencing past end.
    QByteArray a;
    a.append("12\t");
    (void)GitNumstatParser::parse(a);

    QByteArray b;
    b.append("12");
    (void)GitNumstatParser::parse(b);

    QByteArray c;
    c.append("12\t3\tpath_no_nul");
    auto rc = GitNumstatParser::parse(c);
    // Path without trailing NUL: parser falls back to end-of-buffer.
    QCOMPARE(rc.size(), 1);
    QVERIFY(rc.contains(QStringLiteral("path_no_nul")));

    QByteArray d;
    d.append("7\t1\t");
    d.append('\0');
    d.append("orig_only");
    // Missing newPath nul → must bail out, no insert, no crash.
    auto rd = GitNumstatParser::parse(d);
    QCOMPARE(rd.size(), 0);
}

QTEST_GUILESS_MAIN(TestGitNumstatParser)
#include "test_git_numstat_parser.moc"
