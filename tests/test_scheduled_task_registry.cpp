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
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "ApplicationSettings.h"
#include "ScheduledTaskDefinition.h"
#include "ScheduledTaskRegistry.h"


class TestScheduledTaskRegistry : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void emptyOnFirstLaunch();
    void addRemoveRoundTrip();
    void persistsAcrossInstances();
    void enableDisable();
    void signalEmittedOnMutation();
    void duplicateIdRefused();

private:
    QTemporaryDir m_tempDir;
};

void TestScheduledTaskRegistry::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
    QCoreApplication::setOrganizationName("NotepadNextTest");
    QCoreApplication::setApplicationName("NotepadNextTest");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tempDir.path());
}

void TestScheduledTaskRegistry::init()
{
    ApplicationSettings s;
    s.clear();
    s.sync();
}

void TestScheduledTaskRegistry::emptyOnFirstLaunch()
{
    ApplicationSettings s;
    ScheduledTaskRegistry registry(&s);
    QCOMPARE(registry.tasks().size(), 0);
}

void TestScheduledTaskRegistry::addRemoveRoundTrip()
{
    ApplicationSettings s;
    ScheduledTaskRegistry registry(&s);

    ScheduledTaskDefinition def;
    def.id = QStringLiteral("test-1");
    def.name = QStringLiteral("Test Task");
    def.cron = QStringLiteral("*/5 * * * *");
    def.agentId = QStringLiteral("builtin:claude-code");
    def.prompt = QStringLiteral("Hello");

    QVERIFY(registry.addTask(def));
    QCOMPARE(registry.tasks().size(), 1);
    QCOMPARE(registry.task("test-1").name, QStringLiteral("Test Task"));

    QVERIFY(registry.removeTask("test-1"));
    QCOMPARE(registry.tasks().size(), 0);
}

void TestScheduledTaskRegistry::persistsAcrossInstances()
{
    {
        ApplicationSettings s;
        ScheduledTaskRegistry registry(&s);

        ScheduledTaskDefinition def;
        def.id = QStringLiteral("persist-1");
        def.name = QStringLiteral("Persistent");
        def.cron = QStringLiteral("0 9 * * *");
        def.agentId = QStringLiteral("builtin:claude-code");
        def.prompt = QStringLiteral("Run daily");
        def.hasGoalConfig = false;

        QVERIFY(registry.addTask(def));
    }

    // New instance should load the persisted task
    {
        ApplicationSettings s;
        ScheduledTaskRegistry registry(&s);
        QCOMPARE(registry.tasks().size(), 1);
        QCOMPARE(registry.task("persist-1").name, QStringLiteral("Persistent"));
        QCOMPARE(registry.task("persist-1").cron, QStringLiteral("0 9 * * *"));
    }
}

void TestScheduledTaskRegistry::enableDisable()
{
    ApplicationSettings s;
    ScheduledTaskRegistry registry(&s);

    ScheduledTaskDefinition def;
    def.id = QStringLiteral("toggle-1");
    def.name = QStringLiteral("Toggle");
    def.cron = QStringLiteral("0 * * * *");
    def.agentId = QStringLiteral("builtin:claude-code");
    def.prompt = QStringLiteral("test");
    def.enabled = true;

    QVERIFY(registry.addTask(def));
    QVERIFY(registry.task("toggle-1").enabled);

    registry.setEnabled("toggle-1", false);
    QVERIFY(!registry.task("toggle-1").enabled);

    registry.setEnabled("toggle-1", true);
    QVERIFY(registry.task("toggle-1").enabled);
}

void TestScheduledTaskRegistry::signalEmittedOnMutation()
{
    ApplicationSettings s;
    ScheduledTaskRegistry registry(&s);
    QSignalSpy spy(&registry, &ScheduledTaskRegistry::changed);

    ScheduledTaskDefinition def;
    def.id = QStringLiteral("signal-1");
    def.name = QStringLiteral("Signal");
    def.cron = QStringLiteral("0 * * * *");
    def.agentId = QStringLiteral("builtin:claude-code");
    def.prompt = QStringLiteral("test");

    registry.addTask(def);
    QCOMPARE(spy.count(), 1);

    def.name = QStringLiteral("Updated");
    registry.updateTask(def);
    QCOMPARE(spy.count(), 2);

    registry.removeTask("signal-1");
    QCOMPARE(spy.count(), 3);
}

void TestScheduledTaskRegistry::duplicateIdRefused()
{
    ApplicationSettings s;
    ScheduledTaskRegistry registry(&s);

    ScheduledTaskDefinition def;
    def.id = QStringLiteral("dup-1");
    def.name = QStringLiteral("First");
    def.cron = QStringLiteral("0 * * * *");
    def.agentId = QStringLiteral("builtin:claude-code");
    def.prompt = QStringLiteral("test");

    QVERIFY(registry.addTask(def));
    QVERIFY(!registry.addTask(def)); // duplicate
    QCOMPARE(registry.tasks().size(), 1);
}

QTEST_MAIN(TestScheduledTaskRegistry)
#include "test_scheduled_task_registry.moc"
