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
#include <QString>

#include "TerminalAiHelper.h"

class TestTerminalSendToAi : public QObject
{
    Q_OBJECT

private slots:
    void plainText();
    void textWithTrailingNewline();
    void emptyText();
    void onlyNewline();
    void multipleLines();
    void multipleLinesTrailingNewline();
    void containsTripleBacktick();
    void containsQuadBacktick();
    void containsManyBackticks();
    void backtickAtStartAndEnd();
};

void TestTerminalSendToAi::plainText()
{
    QCOMPARE(wrapInCodeblock(QStringLiteral("hello world")),
             QStringLiteral("```\nhello world\n```\n"));
}

void TestTerminalSendToAi::textWithTrailingNewline()
{
    QCOMPARE(wrapInCodeblock(QStringLiteral("hello world\n")),
             QStringLiteral("```\nhello world\n```\n"));
}

void TestTerminalSendToAi::emptyText()
{
    QCOMPARE(wrapInCodeblock(QString()),
             QStringLiteral("```\n\n```\n"));
}

void TestTerminalSendToAi::onlyNewline()
{
    QCOMPARE(wrapInCodeblock(QStringLiteral("\n")),
             QStringLiteral("```\n\n```\n"));
}

void TestTerminalSendToAi::multipleLines()
{
    QCOMPARE(wrapInCodeblock(QStringLiteral("line1\nline2")),
             QStringLiteral("```\nline1\nline2\n```\n"));
}

void TestTerminalSendToAi::multipleLinesTrailingNewline()
{
    QCOMPARE(wrapInCodeblock(QStringLiteral("line1\nline2\n")),
             QStringLiteral("```\nline1\nline2\n```\n"));
}

void TestTerminalSendToAi::containsTripleBacktick()
{
    QCOMPARE(wrapInCodeblock(QStringLiteral("has ``` backticks")),
             QStringLiteral("````\nhas ``` backticks\n````\n"));
}

void TestTerminalSendToAi::containsQuadBacktick()
{
    QCOMPARE(wrapInCodeblock(QStringLiteral("has ```` four")),
             QStringLiteral("`````\nhas ```` four\n`````\n"));
}

void TestTerminalSendToAi::containsManyBackticks()
{
    QCOMPARE(wrapInCodeblock(QStringLiteral("```````ten``````")),
             QStringLiteral("````````\n```````ten``````\n````````\n"));
}

void TestTerminalSendToAi::backtickAtStartAndEnd()
{
    QCOMPARE(wrapInCodeblock(QStringLiteral("```\ncode\n```")),
             QStringLiteral("````\n```\ncode\n```\n````\n"));
}

QTEST_APPLESS_MAIN(TestTerminalSendToAi)
#include "test_terminal_send_to_ai.moc"
