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

#ifndef SHUTDOWNDIAGNOSTICS_H
#define SHUTDOWNDIAGNOSTICS_H

#ifndef NDEBUG

#include <cstdint>

#include <QDateTime>
#include <QMutex>
#include <QString>
#include <QVector>

QT_FORWARD_DECLARE_CLASS(QObject)
QT_FORWARD_DECLARE_CLASS(QEvent)

namespace ShutdownDiagnostics {

struct MemoryStats {
    uint64_t workingSetBytes = 0;
    uint64_t peakWorkingSetBytes = 0;
    uint64_t privateBytes = 0; // Windows only
};

struct ObjectCount {
    QString className;
    int count = 0;
};

struct EventRow {
    QString typeName;
    uint64_t totalCount = 0;
    uint64_t slowCount = 0;
    uint64_t totalNs = 0;
    uint64_t maxNs = 0;
};

struct ProfileRow {
    QString tag;
    uint64_t callCount = 0;
    uint64_t totalNs = 0;
    uint64_t maxNs = 0;
};

struct SlowPaintRow {
    QString className;
    QString objectName;
    uint64_t count = 0;
    uint64_t totalNs = 0;
    uint64_t maxNs = 0;
};

struct DiagnosticReportData {
    QDateTime timestamp;
    QString appVersion;
    qint64 pid = 0;
    qint64 uptimeMs = 0;
    MemoryStats memory;
    QVector<ObjectCount> objectCensus;
    QVector<EventRow> eventRows;
    QVector<ProfileRow> profileRows;
    QVector<SlowPaintRow> slowPaints;
};

// Install: stamps startup time. Call from main() once.
void install();

// Toggle whether the notify() hot path records anything.
void setCollectionEnabled(bool enabled);
bool isCollectionEnabled();

// Threshold above which an event is counted in slowCount.
void setSlowEventThresholdMs(int ms);

// Build report data + format string. Pure functions, testable in isolation.
DiagnosticReportData collect();
QString formatReport(const DiagnosticReportData &data);

// Write report to disk. Returns true on success. Called from aboutToQuit.
bool writeReport();

// Test helper: clears event stats.
void resetForTesting();

namespace Detail {
// Hot-path entry from NotepadNextApplication::notify(). ns = nanoseconds
// elapsed in the QApplication::notify base call.
void recordEvent(QObject *receiver, QEvent *event, qint64 ns);
} // namespace Detail

} // namespace ShutdownDiagnostics

#else // NDEBUG

namespace ShutdownDiagnostics {
inline void install() {}
inline void setCollectionEnabled(bool) {}
inline bool isCollectionEnabled() { return false; }
inline void setSlowEventThresholdMs(int) {}
inline bool writeReport() { return false; }
} // namespace ShutdownDiagnostics

#endif // NDEBUG

#endif // SHUTDOWNDIAGNOSTICS_H
