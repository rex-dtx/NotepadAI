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
#include <QDateTime>
#include <QTimeZone>

#include "CronExpression.h"


class TestCronExpression : public QObject
{
    Q_OBJECT

private slots:
    void parseValid_standard();
    void parseValid_ranges();
    void parseValid_steps();
    void parseValid_lists();
    void parseInvalid_empty();
    void parseInvalid_tooFewFields();
    void parseInvalid_badRange();
    void nextFireTime_returnsCorrectValue();
    void nextFireTime_invalidForImpossibleDate();
    void nextFireTimes_returnsCorrectCount();
};

void TestCronExpression::parseValid_standard()
{
    auto expr = CronExpression::parse(QStringLiteral("0 * * * *"));
    QVERIFY(expr.has_value());
    QVERIFY(expr->isValid());
}

void TestCronExpression::parseValid_ranges()
{
    auto expr = CronExpression::parse(QStringLiteral("0 9-17 * * *"));
    QVERIFY(expr.has_value());
    QVERIFY(expr->isValid());
}

void TestCronExpression::parseValid_steps()
{
    auto expr = CronExpression::parse(QStringLiteral("*/5 * * * *"));
    QVERIFY(expr.has_value());
    QVERIFY(expr->isValid());
}

void TestCronExpression::parseValid_lists()
{
    auto expr = CronExpression::parse(QStringLiteral("0 9,12,18 * * *"));
    QVERIFY(expr.has_value());
    QVERIFY(expr->isValid());
}

void TestCronExpression::parseInvalid_empty()
{
    auto expr = CronExpression::parse(QString());
    QVERIFY(!expr.has_value());
}

void TestCronExpression::parseInvalid_tooFewFields()
{
    auto expr = CronExpression::parse(QStringLiteral("0 *"));
    QVERIFY(!expr.has_value());
}

void TestCronExpression::parseInvalid_badRange()
{
    auto expr = CronExpression::parse(QStringLiteral("99 * * * *"));
    QVERIFY(!expr.has_value());
}

void TestCronExpression::nextFireTime_returnsCorrectValue()
{
    // Every hour at minute 0
    auto expr = CronExpression::parse(QStringLiteral("0 * * * *"));
    QVERIFY(expr.has_value());

    // From 2026-01-15 10:30:00 -> next should be 2026-01-15 11:00:00
    QDateTime from(QDate(2026, 1, 15), QTime(10, 30, 0));
    QDateTime next = expr->nextFireTime(from);
    QVERIFY(next.isValid());
    QCOMPARE(next.time().minute(), 0);
    QVERIFY(next > from);
}

void TestCronExpression::nextFireTime_invalidForImpossibleDate()
{
    // Feb 30 does not exist — "0 0 30 2 *"
    auto expr = CronExpression::parse(QStringLiteral("0 0 30 2 *"));
    QVERIFY(expr.has_value());

    QDateTime from(QDate(2026, 1, 1), QTime(0, 0, 0));
    QDateTime next = expr->nextFireTime(from);
    // supertinycron returns -1 for impossible dates
    QVERIFY(!next.isValid());
}

void TestCronExpression::nextFireTimes_returnsCorrectCount()
{
    auto expr = CronExpression::parse(QStringLiteral("*/10 * * * *"));
    QVERIFY(expr.has_value());

    QDateTime from(QDate(2026, 1, 15), QTime(10, 0, 0));
    auto times = expr->nextFireTimes(from, 5);
    QCOMPARE(times.size(), 5);

    // Each should be 10 minutes apart
    for (int i = 1; i < times.size(); ++i) {
        QCOMPARE(times[i - 1].secsTo(times[i]), 600);
    }
}

QTEST_MAIN(TestCronExpression)
#include "test_cron_expression.moc"
