/*
 * This file is part of Notepad Next.
 * Copyright 2019 Justin Dailey
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


#include <QDebug>
#include <QSettings>
#include <QSysInfo>
#include <QApplication>
#include <QDataStream>

#include <cstring>

#include "CrashHandler.h"
#include "NotepadNextApplication.h"
#include "ShutdownDiagnostics.h"

int main(int argc, char *argv[])
{
    // Install crash handlers before anything else so faults during early init
    // (Qt plugin load, settings parse, ...) still produce a crash_report.txt.
    CrashHandler::install();

    // Stamp startup time for debug-build shutdown diagnostics. No-op in Release.
    ShutdownDiagnostics::install();

    // Test-only CLI flag wired up to scripts/test-crash-handler.sh. Parsed
    // before QApplication so we don't depend on SingleApplication forwarding.
    for (int i = 1; i < argc; ++i) {
        const char *prefix = "--__trigger-crash=";
        if (std::strncmp(argv[i], prefix, std::strlen(prefix)) == 0) {
            CrashHandler::triggerCrashForTest(argv[i] + std::strlen(prefix));
        }
    }

    qSetMessagePattern("[%{time process}] %{if-debug}D%{endif}%{if-info}I%{endif}%{if-warning}W%{endif}%{if-critical}C%{endif}%{if-fatal}F%{endif}: %{message}");

    // Set these since other parts of the app references these
    QApplication::setOrganizationName("NotepadAI");
    QApplication::setApplicationName("NotepadAI");
    QGuiApplication::setApplicationDisplayName("NotepadAI");
    QGuiApplication::setApplicationVersion(APP_VERSION);

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

    // Default settings format
    QSettings::setDefaultFormat(QSettings::IniFormat);

    NotepadNextApplication app(argc, argv);

    // Log some debug info
    qInfo("=============================");
    for(const auto &d : app.debugInfo()){
        qInfo("%s", qUtf8Printable(d));
    }
    qInfo("=============================");


    if(app.isPrimary()) {
        app.init();

        return app.exec();
    }
    else {
        qInfo() << "Primary instance already running. PID:" << app.primaryPid();

        app.sendInfoToPrimaryInstance();

        qInfo() << "Secondary instance closing...";

        app.exit(0);

        return 0;
    }
}
