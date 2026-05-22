/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * SPDX short: GPL-3.0-or-later
 */

#include <QtTest>
#include <QLabel>
#include <QProgressBar>

#include "AcpUsageIndicator.h"

class TestAcpUsageIndicator : public QObject
{
    Q_OBJECT

private slots:
    void hidden_when_no_usage();
    void label_and_bar_shown_with_full_usage();
    void warning_level_at_80_percent();
    void error_level_at_or_above_100_percent();
    void label_only_when_no_max();
    void label_includes_cost_when_present();

private:
    template<typename T>
    static T *findChildOfType(QWidget *parent) {
        return parent->findChild<T *>();
    }
};

void TestAcpUsageIndicator::hidden_when_no_usage()
{
    AcpUsageIndicator w;
    w.setUsage(std::nullopt);
    auto *label = w.findChild<QLabel *>();
    auto *bar = w.findChild<QProgressBar *>();
    QVERIFY(label);
    QVERIFY(bar);
    QVERIFY(!label->isVisibleTo(&w));
    QVERIFY(!bar->isVisibleTo(&w));
}

void TestAcpUsageIndicator::label_and_bar_shown_with_full_usage()
{
    AcpUsageIndicator w;
    AcpProtocol::AcpUsage usage;
    usage.inputTokens = 1234;
    usage.outputTokens = 567;
    usage.totalTokens = 1801;
    usage.maxTokens = 100000;
    w.setUsage(usage);

    auto *label = w.findChild<QLabel *>();
    auto *bar = w.findChild<QProgressBar *>();
    QVERIFY(label);
    QVERIFY(bar);
    QVERIFY(!label->isHidden());
    QVERIFY(!bar->isHidden());
    // Single total label, formatted as "X.Xk".
    QCOMPARE(label->text(), QStringLiteral("1.8k"));
    QCOMPARE(bar->value(), 1801);
    QCOMPARE(bar->maximum(), 100000);
}

void TestAcpUsageIndicator::warning_level_at_80_percent()
{
    AcpUsageIndicator w;
    AcpProtocol::AcpUsage usage;
    usage.inputTokens = 80000;
    usage.outputTokens = 0;
    usage.maxTokens = 100000;
    w.setUsage(usage);

    auto *bar = w.findChild<QProgressBar *>();
    QVERIFY(bar);
    QCOMPARE(bar->property("level").toString(), QStringLiteral("warning"));
}

void TestAcpUsageIndicator::error_level_at_or_above_100_percent()
{
    AcpUsageIndicator w;
    AcpProtocol::AcpUsage usage;
    usage.inputTokens = 110000;
    usage.outputTokens = 0;
    usage.maxTokens = 100000;
    w.setUsage(usage);

    auto *bar = w.findChild<QProgressBar *>();
    QVERIFY(bar);
    QCOMPARE(bar->property("level").toString(), QStringLiteral("error"));
}

void TestAcpUsageIndicator::label_only_when_no_max()
{
    AcpUsageIndicator w;
    AcpProtocol::AcpUsage usage;
    usage.inputTokens = 100;
    usage.outputTokens = 100;
    // maxTokens left as nullopt
    w.setUsage(usage);

    auto *label = w.findChild<QLabel *>();
    auto *bar = w.findChild<QProgressBar *>();
    QVERIFY(label);
    QVERIFY(bar);
    QVERIFY(!label->isHidden());
    QVERIFY(bar->isHidden());
}

void TestAcpUsageIndicator::label_includes_cost_when_present()
{
    AcpUsageIndicator w;
    AcpProtocol::AcpUsage usage;
    usage.totalTokens = 44030;
    usage.costAmount = 0.29799275;
    usage.costCurrency = QStringLiteral("USD");
    w.setUsage(usage);

    auto *label = w.findChild<QLabel *>();
    QVERIFY(label);
    QCOMPARE(label->text(), QStringLiteral("44.0k (0.3usd)"));
}

QTEST_MAIN(TestAcpUsageIndicator)
#include "test_acp_usage_indicator.moc"

