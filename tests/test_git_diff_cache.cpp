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

#include "GitDiffCache.h"
#include "GitDiffParser.h"

#include <memory>

class TestGitDiffCache : public QObject
{
    Q_OBJECT

private slots:
    void keyFor_stableAndUnique();
    void getMissing_returnsNull();
    void putAndGet_roundTrip();
    void put_replacesExisting();
    void lru_orderingUpdatedOnGet();
    void byteBudget_evictsLruFirst();
    void clear_dropsEverything();
};

namespace {

GitDiffCache::Entry makeEntry(int rows = 1)
{
    auto r = std::make_shared<GitDiffParser::Result>();
    r->texts.reserve(rows);
    for (int i = 0; i < rows; ++i) r->texts.push_back(QByteArrayLiteral("x"));
    r->kinds.fill(GitDiffParser::LineKind::Context, rows);
    r->oldLn.fill(-1, rows);
    r->newLn.fill(-1, rows);
    return r;
}

} // namespace

void TestGitDiffCache::keyFor_stableAndUnique()
{
    const QString repo = QStringLiteral("/tmp/repo");
    const QString a = QStringLiteral("src/a.cpp");
    const QString b = QStringLiteral("src/b.cpp");

    // Same input → same key (stable hashing).
    QCOMPARE(GitDiffCache::keyFor(repo, a, false),
             GitDiffCache::keyFor(repo, a, false));

    // Staged flag changes the key.
    QVERIFY(GitDiffCache::keyFor(repo, a, false)
            != GitDiffCache::keyFor(repo, a, true));

    // Different paths produce different keys.
    QVERIFY(GitDiffCache::keyFor(repo, a, false)
            != GitDiffCache::keyFor(repo, b, false));

    // Different repo produces different keys (guards against collisions when
    // multiple workspaces share path suffixes).
    QVERIFY(GitDiffCache::keyFor(repo, a, false)
            != GitDiffCache::keyFor(QStringLiteral("/tmp/other"), a, false));
}

void TestGitDiffCache::getMissing_returnsNull()
{
    GitDiffCache cache(1024);
    QVERIFY(!cache.get(0xdeadbeef));
    QVERIFY(!cache.get(0));
}

void TestGitDiffCache::putAndGet_roundTrip()
{
    GitDiffCache cache(1024 * 1024);
    auto entry = makeEntry();
    cache.put(42, entry, 100);

    auto got = cache.get(42);
    QVERIFY(got);
    QCOMPARE(got.get(), entry.get());
}

void TestGitDiffCache::put_replacesExisting()
{
    GitDiffCache cache(1024 * 1024);
    auto first = makeEntry(1);
    auto second = makeEntry(2);

    cache.put(7, first, 50);
    const qsizetype afterFirst = cache.sizeBytes();
    QVERIFY(afterFirst > 0);

    cache.put(7, second, 200);
    auto got = cache.get(7);
    QCOMPARE(got.get(), second.get());
    // Replacement must account for the old footprint being removed first —
    // otherwise sizeBytes would double-count.
    QCOMPARE(cache.sizeBytes(), (qsizetype)((200 * 13) / 10));
}

void TestGitDiffCache::lru_orderingUpdatedOnGet()
{
    // capacity exactly fits two entries of footprint = ceil(bytes*1.3); pick 100b
    // each → 130b each → capacity 260b. Inserting a third must evict the LRU.
    GitDiffCache cache(260);
    auto e1 = makeEntry();
    auto e2 = makeEntry();
    auto e3 = makeEntry();

    cache.put(1, e1, 100);
    cache.put(2, e2, 100);

    // Touch key 1 — moves it to MRU, so key 2 becomes LRU.
    QVERIFY(cache.get(1));

    cache.put(3, e3, 100);

    // Key 2 should have been evicted, key 1 + key 3 still present.
    QVERIFY(cache.get(1));
    QVERIFY(!cache.get(2));
    QVERIFY(cache.get(3));
}

void TestGitDiffCache::byteBudget_evictsLruFirst()
{
    GitDiffCache cache(260); // ~2 slots at 100 raw bytes each
    auto e1 = makeEntry();
    auto e2 = makeEntry();
    auto e3 = makeEntry();

    cache.put(10, e1, 100);
    cache.put(20, e2, 100);
    cache.put(30, e3, 100); // pushes total to 390b → must evict key 10 (LRU).

    QVERIFY(!cache.get(10));
    QVERIFY(cache.get(20));
    QVERIFY(cache.get(30));
    QVERIFY(cache.sizeBytes() <= cache.capacityBytes());
}

void TestGitDiffCache::clear_dropsEverything()
{
    GitDiffCache cache(1024 * 1024);
    cache.put(1, makeEntry(), 100);
    cache.put(2, makeEntry(), 100);
    QVERIFY(cache.sizeBytes() > 0);

    cache.clear();
    QCOMPARE(cache.sizeBytes(), (qsizetype)0);
    QVERIFY(!cache.get(1));
    QVERIFY(!cache.get(2));

    // Cache is reusable after clear.
    cache.put(3, makeEntry(), 50);
    QVERIFY(cache.get(3));
}

QTEST_GUILESS_MAIN(TestGitDiffCache)
#include "test_git_diff_cache.moc"
