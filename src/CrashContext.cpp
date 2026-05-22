/*
 * NotepadADE addition: implementation of CrashContext buffers. See header for
 * design rationale. UTF-8 boundary walk-back logic below ensures the crash
 * handler never reads a half-encoded sequence even when truncation cuts the
 * middle of a multi-byte character (common for Vietnamese action names).
 */

#include "CrashContext.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>

#include <cstring>

namespace CrashContext {

char lastAction[LastActionSize]                = {0};
char activeEditorPath[EditorPathSize]          = {0};
char activeDockId[DockIdSize]                  = {0};
char activeWorkspaceRoot[WorkspaceRootSize]    = {0};
std::atomic<unsigned long long> lastActionTimestampMs{0};

namespace {

// Trim `buf` so that no trailing UTF-8 continuation byte (10xxxxxx) is left
// dangling at the end. After this call, `buf` is either an empty string or
// ends on a complete UTF-8 character. Caller is expected to have already
// strncpy'd and NUL-terminated.
void trimToUtf8Boundary(char *buf, std::size_t capacity)
{
    if (capacity == 0) return;
    buf[capacity - 1] = '\0';

    std::size_t len = std::strlen(buf);
    if (len == 0) return;

    // Skip back over continuation bytes (10xxxxxx) to find the lead byte.
    std::size_t i = len;
    while (i > 0 && (static_cast<unsigned char>(buf[i - 1]) & 0xC0) == 0x80) {
        --i;
    }
    if (i == 0) {
        // All continuation bytes => corrupt; drop everything.
        buf[0] = '\0';
        return;
    }

    const unsigned char lead = static_cast<unsigned char>(buf[i - 1]);
    int expected = 0;
    if (lead < 0x80)              expected = 1;
    else if ((lead & 0xE0) == 0xC0) expected = 2;
    else if ((lead & 0xF0) == 0xE0) expected = 3;
    else if ((lead & 0xF8) == 0xF0) expected = 4;
    else                            expected = 0; // invalid lead byte

    const std::size_t actual = len - (i - 1);
    if (expected == 0 || actual < static_cast<std::size_t>(expected)) {
        buf[i - 1] = '\0';
    }
}

void storeUtf8(char *buf, std::size_t capacity, const QString &src)
{
    const QByteArray utf8 = src.toUtf8();
    const std::size_t n = static_cast<std::size_t>(utf8.size());
    const std::size_t copy = (n < capacity - 1) ? n : capacity - 1;
    std::memcpy(buf, utf8.constData(), copy);
    buf[copy] = '\0';
    trimToUtf8Boundary(buf, capacity);
}

} // namespace

void setLastAction(const QString &name)
{
    storeUtf8(lastAction, LastActionSize, name);
    lastActionTimestampMs.store(
        static_cast<unsigned long long>(QDateTime::currentMSecsSinceEpoch()),
        std::memory_order_relaxed);
}

void setActiveEditorPath(const QString &path)
{
    storeUtf8(activeEditorPath, EditorPathSize, path);
}

void setActiveDockId(const QString &dockObjectName)
{
    storeUtf8(activeDockId, DockIdSize, dockObjectName);
}

void setActiveWorkspaceRoot(const QString &root)
{
    storeUtf8(activeWorkspaceRoot, WorkspaceRootSize, root);
}

} // namespace CrashContext
