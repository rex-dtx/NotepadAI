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

#include "RecursiveTreeWatcher.h"

#ifdef Q_OS_WIN

#include <QMetaObject>
#include <QStringView>

#include <qt_windows.h>

namespace {
constexpr DWORD kFilter = FILE_NOTIFY_CHANGE_FILE_NAME
                        | FILE_NOTIFY_CHANGE_DIR_NAME
                        | FILE_NOTIFY_CHANGE_LAST_WRITE;
constexpr DWORD kBufSize = 32768;
} // namespace

RecursiveTreeWatcher::RecursiveTreeWatcher(QObject *parent)
    : QObject(parent)
{
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

RecursiveTreeWatcher::~RecursiveTreeWatcher()
{
    stop();
    if (m_stopEvent)
        CloseHandle(static_cast<HANDLE>(m_stopEvent));
}

void RecursiveTreeWatcher::start(const QString &path)
{
    stop();

    m_stop.store(false, std::memory_order_relaxed);
    m_pendingNotify.store(false, std::memory_order_relaxed);
    ResetEvent(static_cast<HANDLE>(m_stopEvent));

    HANDLE stopEvt = static_cast<HANDLE>(m_stopEvent);
    std::atomic<bool> *stopFlag = &m_stop;
    std::atomic<bool> *pendingFlag = &m_pendingNotify;
    RecursiveTreeWatcher *self = this;

    m_thread = QThread::create([self, path, stopEvt, stopFlag, pendingFlag]() {
        const std::wstring wpath = path.toStdWString();
        HANDLE hDir = CreateFileW(
            wpath.c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            nullptr);

        if (hDir == INVALID_HANDLE_VALUE)
            return;

        alignas(DWORD) char buf[kBufSize];
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) {
            CloseHandle(hDir);
            return;
        }

        while (!stopFlag->load(std::memory_order_relaxed)) {
            ResetEvent(ov.hEvent);
            BOOL ok = ReadDirectoryChangesW(
                hDir, buf, kBufSize, TRUE, kFilter, nullptr, &ov, nullptr);
            if (!ok)
                break;

            HANDLE handles[2] = { ov.hEvent, stopEvt };
            DWORD wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            if (wait != WAIT_OBJECT_0)
                break;

            DWORD bytes = 0;
            if (!GetOverlappedResult(hDir, &ov, &bytes, FALSE))
                continue;

            // A successful read returning zero bytes means the 32KB buffer
            // overflowed and the OS discarded the entire batch (bulk changes:
            // npm install, large git checkout, build output, agent swarms).
            // Treat it as "something changed, contents unknown" and notify so
            // the panel still refreshes when it matters most.
            if (bytes == 0) {
                if (!pendingFlag->exchange(true, std::memory_order_acq_rel))
                    QMetaObject::invokeMethod(self, &RecursiveTreeWatcher::changed,
                                              Qt::QueuedConnection);
                continue;
            }

            bool relevant = false;
            auto *info = reinterpret_cast<FILE_NOTIFY_INFORMATION *>(buf);
            for (;;) {
                const int nameLen = static_cast<int>(info->FileNameLength / sizeof(WCHAR));
                const QStringView rel(info->FileName, nameLen);
                if (!rel.startsWith(u".git/") && !rel.startsWith(u".git\\")) {
                    relevant = true;
                    break;
                }
                if (info->NextEntryOffset == 0)
                    break;
                info = reinterpret_cast<FILE_NOTIFY_INFORMATION *>(
                    reinterpret_cast<char *>(info) + info->NextEntryOffset);
            }

            if (relevant && !pendingFlag->exchange(true, std::memory_order_acq_rel))
                QMetaObject::invokeMethod(self, &RecursiveTreeWatcher::changed,
                                          Qt::QueuedConnection);
        }

        CancelIo(hDir);
        CloseHandle(ov.hEvent);
        CloseHandle(hDir);
    });

    m_thread->start();
}

void RecursiveTreeWatcher::stop()
{
    if (!m_thread)
        return;
    // The worker may have self-exited (folder unmounted, event-creation or
    // ReadDirectoryChangesW failure), leaving isRunning() false but m_thread
    // still pointing at a finished QThread. Always delete it here so a later
    // start() doesn't overwrite (and leak) the stale handle.
    if (m_thread->isRunning()) {
        m_stop.store(true, std::memory_order_relaxed);
        SetEvent(static_cast<HANDLE>(m_stopEvent));
        m_thread->wait();
    }
    delete m_thread;
    m_thread = nullptr;
}

#endif // Q_OS_WIN
