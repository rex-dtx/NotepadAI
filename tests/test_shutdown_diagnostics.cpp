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
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QTemporaryDir>

#include "ShutdownDiagnostics.h"
#include "ProfileScope.h"

class TestShutdownDiagnostics : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void collectionGateDefaultsOn();
    void setCollectionEnabled_TogglesGate();
    void formatReport_ContainsAllSections();
    void formatReport_EmptyProfileSection();
    void writeReport_CreatesFile();
    void collect_PopulatesPidAndUptime();
    void recordEvent_RespectsWhitelist();
    void recordEvent_CountsSlowAboveThreshold();
};

void TestShutdownDiagnostics::initTestCase()
{
    ShutdownDiagnostics::install();
}

void TestShutdownDiagnostics::init()
{
    ShutdownDiagnostics::setCollectionEnabled(true);
    ShutdownDiagnostics::setSlowEventThresholdMs(16);
    ShutdownDiagnostics::resetForTesting();
    ProfileScope::resetForTesting();
}

void TestShutdownDiagnostics::collectionGateDefaultsOn()
{
    QVERIFY(ShutdownDiagnostics::isCollectionEnabled());
}

void TestShutdownDiagnostics::setCollectionEnabled_TogglesGate()
{
    ShutdownDiagnostics::setCollectionEnabled(false);
    QVERIFY(!ShutdownDiagnostics::isCollectionEnabled());
    ShutdownDiagnostics::setCollectionEnabled(true);
    QVERIFY(ShutdownDiagnostics::isCollectionEnabled());
}

void TestShutdownDiagnostics::formatReport_ContainsAllSections()
{
    const auto data = ShutdownDiagnostics::collect();
    const QString out = ShutdownDiagnostics::formatReport(data);

    QVERIFY(out.contains(QStringLiteral("SHUTDOWN DIAGNOSTICS")));
    QVERIFY(out.contains(QStringLiteral("Memory")));
    QVERIFY(out.contains(QStringLiteral("QObject census")));
    QVERIFY(out.contains(QStringLiteral("Slow events")));
    QVERIFY(out.contains(QStringLiteral("PROFILE_SCOPE aggregates")));
    QVERIFY(out.contains(QStringLiteral("END")));
}

void TestShutdownDiagnostics::formatReport_EmptyProfileSection()
{
    const auto data = ShutdownDiagnostics::collect();
    const QString out = ShutdownDiagnostics::formatReport(data);
    QVERIFY(out.contains(QStringLiteral("(no PROFILE_SCOPE samples)")));
}

void TestShutdownDiagnostics::writeReport_CreatesFile()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString savedCwd = QDir::currentPath();
    QVERIFY(QDir::setCurrent(tmp.path()));

    const bool ok = ShutdownDiagnostics::writeReport();
    QVERIFY(ok);

    QFile f(tmp.path() + QStringLiteral("/shutdown_report.txt"));
    QVERIFY(f.exists());
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString body = QString::fromUtf8(f.readAll());
    f.close();
    QVERIFY(body.contains(QStringLiteral("SHUTDOWN DIAGNOSTICS")));

    QVERIFY(QDir::setCurrent(savedCwd));
}

void TestShutdownDiagnostics::collect_PopulatesPidAndUptime()
{
    const auto data = ShutdownDiagnostics::collect();
    QVERIFY(data.pid > 0);
    QVERIFY(data.uptimeMs >= 0);
    QVERIFY(!data.timestamp.isNull());
    QVERIFY(!data.appVersion.isEmpty());
}

void TestShutdownDiagnostics::recordEvent_RespectsWhitelist()
{
    // KeyPress is whitelisted; LocaleChange is not (an arbitrary type outside
    // the whitelist).
    QKeyEvent keyEvent(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
    QEvent localeEvent(QEvent::LocaleChange);

    ShutdownDiagnostics::Detail::recordEvent(this, &keyEvent, 1000);
    ShutdownDiagnostics::Detail::recordEvent(this, &localeEvent, 1000);

    const auto data = ShutdownDiagnostics::collect();
    // Only KeyPress should appear.
    bool sawKey = false;
    bool sawLocale = false;
    for (const auto &r : data.eventRows) {
        if (r.typeName == QStringLiteral("KeyPress")) sawKey = true;
        if (r.typeName == QStringLiteral("LocaleChange")) sawLocale = true;
    }
    QVERIFY(sawKey);
    QVERIFY(!sawLocale);
}

void TestShutdownDiagnostics::recordEvent_CountsSlowAboveThreshold()
{
    ShutdownDiagnostics::setSlowEventThresholdMs(10); // 10 ms = 10,000,000 ns

    QKeyEvent ev(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
    ShutdownDiagnostics::Detail::recordEvent(this, &ev, 1'000'000);     // 1 ms — fast
    ShutdownDiagnostics::Detail::recordEvent(this, &ev, 15'000'000);    // 15 ms — slow
    ShutdownDiagnostics::Detail::recordEvent(this, &ev, 50'000'000);    // 50 ms — slow

    const auto data = ShutdownDiagnostics::collect();
    bool found = false;
    for (const auto &r : data.eventRows) {
        if (r.typeName == QStringLiteral("KeyPress")) {
            QCOMPARE(r.totalCount, quint64(3));
            QCOMPARE(r.slowCount, quint64(2));
            QVERIFY(r.maxNs >= 50'000'000ULL);
            found = true;
        }
    }
    QVERIFY(found);
}

QTEST_MAIN(TestShutdownDiagnostics)

#include "test_shutdown_diagnostics.moc"
