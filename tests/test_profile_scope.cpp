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

#include <QtTest>
#include <chrono>
#include <thread>

#include "ProfileScope.h"

class TestProfileScope : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void emptySnapshot();
    void singleScopeIncrementsCallCount();
    void multipleScopesAccumulateTime();
    void sameTagAcrossSlotsAggregates();
    void snapshotSortedByTotalDesc();
    void maxNsTracksLargest();
};

void TestProfileScope::init()
{
    ProfileScope::resetForTesting();
}

void TestProfileScope::emptySnapshot()
{
    QVERIFY(ProfileScope::snapshot().empty());
}

void TestProfileScope::singleScopeIncrementsCallCount()
{
    {
        PROFILE_SCOPE("test.A");
    }
    const auto s = ProfileScope::snapshot();
    QCOMPARE(s.size(), size_t(1));
    QCOMPARE(s[0].tag, QStringLiteral("test.A"));
    QCOMPARE(s[0].callCount, quint64(1));
    QVERIFY(s[0].totalNs >= 0);
}

void TestProfileScope::multipleScopesAccumulateTime()
{
    for (int i = 0; i < 10; ++i) {
        PROFILE_SCOPE("test.B");
    }
    const auto s = ProfileScope::snapshot();
    QCOMPARE(s.size(), size_t(1));
    QCOMPARE(s[0].callCount, quint64(10));
}

void TestProfileScope::sameTagAcrossSlotsAggregates()
{
    // Two different call sites with the same tag string should still aggregate
    // into the same slot via acquireSlot's tag-keyed map.
    {
        PROFILE_SCOPE("shared.tag");
    }
    {
        PROFILE_SCOPE("shared.tag");
    }
    const auto s = ProfileScope::snapshot();
    QCOMPARE(s.size(), size_t(1));
    QCOMPARE(s[0].callCount, quint64(2));
}

void TestProfileScope::snapshotSortedByTotalDesc()
{
    {
        PROFILE_SCOPE("fast");
    }
    // Make 'slow' run longer than 'fast' by adding a small busy wait.
    {
        PROFILE_SCOPE("slow");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto s = ProfileScope::snapshot();
    QCOMPARE(s.size(), size_t(2));
    // 'slow' should have larger totalNs and be first.
    QCOMPARE(s[0].tag, QStringLiteral("slow"));
    QCOMPARE(s[1].tag, QStringLiteral("fast"));
    QVERIFY(s[0].totalNs >= s[1].totalNs);
}

void TestProfileScope::maxNsTracksLargest()
{
    {
        PROFILE_SCOPE("maxtest");
    }
    {
        PROFILE_SCOPE("maxtest");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    {
        PROFILE_SCOPE("maxtest");
    }
    const auto s = ProfileScope::snapshot();
    QCOMPARE(s.size(), size_t(1));
    QCOMPARE(s[0].callCount, quint64(3));
    // The 5ms iteration should dominate maxNs.
    QVERIFY(s[0].maxNs >= 1'000'000ULL); // at least 1 ms
}

QTEST_APPLESS_MAIN(TestProfileScope)

#include "test_profile_scope.moc"
