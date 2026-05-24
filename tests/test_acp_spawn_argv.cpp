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

#include "AcpProtocol.h"

class TestAcpSpawnArgv : public QObject
{
    Q_OBJECT

private slots:
    void posixBasic();
    void posixWithSingleQuote();
    void posixUsesShellEnv();
    void posixFallsBackToBinSh();
    void windowsCmd();
    void windowsCmdSpacedPath();
    void windowsPs1();
    void windowsExe();
};

void TestAcpSpawnArgv::posixBasic()
{
    auto p = AcpProtocol::buildSpawnArgv(QStringLiteral("npx"),
                                         QStringList{QStringLiteral("-y"), QStringLiteral("x")},
                                         true);
    // CI hosts may have arbitrary $SHELL — the only guarantee is non-empty
    // program plus the -lc flag plus the quoted command line.
    QVERIFY(!p.program.isEmpty());
    QVERIFY(p.nativeArgumentsLine.isEmpty());
    QCOMPARE(p.arguments.size(), 2);
    QCOMPARE(p.arguments.at(0), QStringLiteral("-lc"));
    // npx '-y' 'x'
    QVERIFY(p.arguments.at(1).contains(QStringLiteral("npx")));
    QVERIFY(p.arguments.at(1).contains(QStringLiteral("'-y'")));
    QVERIFY(p.arguments.at(1).contains(QStringLiteral("'x'")));
}

void TestAcpSpawnArgv::posixWithSingleQuote()
{
    auto p = AcpProtocol::buildSpawnArgv(QStringLiteral("foo"),
                                         QStringList{QStringLiteral("a'b")},
                                         true);
    // expect 'a'\''b'
    QVERIFY(p.arguments.at(1).contains(QStringLiteral("'a'\\''b'")));
}

void TestAcpSpawnArgv::posixUsesShellEnv()
{
    // Snapshot prior value so we can restore.
    const bool hadShell = qEnvironmentVariableIsSet("SHELL");
    const QByteArray prior = hadShell ? qgetenv("SHELL") : QByteArray();

    qputenv("SHELL", "/usr/bin/zsh");
    auto p = AcpProtocol::buildSpawnArgv(QStringLiteral("npx"),
                                         QStringList{QStringLiteral("a")},
                                         true);
    QCOMPARE(p.program, QStringLiteral("/usr/bin/zsh"));
    QCOMPARE(p.arguments.at(0), QStringLiteral("-lc"));

    if (hadShell) {
        qputenv("SHELL", prior);
    } else {
        qunsetenv("SHELL");
    }
}

void TestAcpSpawnArgv::posixFallsBackToBinSh()
{
    const bool hadShell = qEnvironmentVariableIsSet("SHELL");
    const QByteArray prior = hadShell ? qgetenv("SHELL") : QByteArray();

    qunsetenv("SHELL");
    auto p = AcpProtocol::buildSpawnArgv(QStringLiteral("npx"),
                                         QStringList{QStringLiteral("a")},
                                         true);
    QCOMPARE(p.program, QStringLiteral("/bin/sh"));
    QCOMPARE(p.arguments.at(0), QStringLiteral("-lc"));

    if (hadShell) {
        qputenv("SHELL", prior);
    }
}

void TestAcpSpawnArgv::windowsCmd()
{
    auto p = AcpProtocol::buildSpawnArgv(QStringLiteral("npx"),
                                         QStringList{QStringLiteral("-y")},
                                         false,
                                         QStringLiteral("C:/x/npx.cmd"));
    QCOMPARE(p.program, QStringLiteral("cmd"));
    QVERIFY(p.arguments.isEmpty());
    // No spaces in the resolved path → no need to quote it. Args still go through
    // the Windows quoter; "-y" has no whitespace so it stays bare.
    QCOMPARE(p.nativeArgumentsLine,
             QStringLiteral("/D /S /C \"C:/x/npx.cmd -y\""));
}

void TestAcpSpawnArgv::windowsCmdSpacedPath()
{
    // Regression: "C:/Program Files/nodejs/npx.cmd" must end up double-quoted
    // inside the cmd /C "<line>" wrapper so cmd.exe doesn't fragment on the
    // space and try to launch "C:/Program".
    auto p = AcpProtocol::buildSpawnArgv(QStringLiteral("npx"),
                                         QStringList{QStringLiteral("-y"),
                                                     QStringLiteral("@zed-industries/codex-acp@latest")},
                                         false,
                                         QStringLiteral("C:/Program Files/nodejs/npx.cmd"));
    QCOMPARE(p.program, QStringLiteral("cmd"));
    QVERIFY(p.arguments.isEmpty());
    QCOMPARE(p.nativeArgumentsLine,
             QStringLiteral("/D /S /C \"\"C:/Program Files/nodejs/npx.cmd\" -y @zed-industries/codex-acp@latest\""));
}

void TestAcpSpawnArgv::windowsPs1()
{
    auto p = AcpProtocol::buildSpawnArgv(QStringLiteral("script"),
                                         QStringList{QStringLiteral("a")},
                                         false,
                                         QStringLiteral("C:/x/script.ps1"));
    QCOMPARE(p.program, QStringLiteral("powershell"));
    QVERIFY(p.nativeArgumentsLine.isEmpty());
    QCOMPARE(p.arguments.size(), 4);
    QCOMPARE(p.arguments.at(0), QStringLiteral("-NoProfile"));
    QCOMPARE(p.arguments.at(1), QStringLiteral("-File"));
    QCOMPARE(p.arguments.at(2), QStringLiteral("C:/x/script.ps1"));
    QCOMPARE(p.arguments.at(3), QStringLiteral("a"));
}

void TestAcpSpawnArgv::windowsExe()
{
    auto p = AcpProtocol::buildSpawnArgv(QStringLiteral("tool"),
                                         QStringList{QStringLiteral("a"), QStringLiteral("b")},
                                         false,
                                         QStringLiteral("C:/x/tool.exe"));
    QCOMPARE(p.program, QStringLiteral("C:/x/tool.exe"));
    QVERIFY(p.nativeArgumentsLine.isEmpty());
    QCOMPARE(p.arguments, (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
}

QTEST_GUILESS_MAIN(TestAcpSpawnArgv)
#include "test_acp_spawn_argv.moc"
