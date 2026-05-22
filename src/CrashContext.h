/*
 * NotepadADE addition: thread-shared static buffers describing what the user
 * was doing when the process died. Read from inside CrashHandler signal/SEH
 * paths (async-signal context), so updates must be deterministic and avoid
 * leaving the buffer in a state the handler can't decode.
 *
 * Writers (UI thread): setLastAction / setActiveEditorPath / setActiveDockId /
 * setActiveWorkspaceRoot. Each truncates to a fixed-size static buffer and
 * walks back to the nearest UTF-8 sequence boundary so the trailing byte is
 * never half of a multi-byte character.
 *
 * Reader (CrashHandler): exported buffers are zero-padded char arrays; the
 * handler memcpy's them into its pre-allocated report buffer without calling
 * any non-async-safe API.
 */

#ifndef CRASHCONTEXT_H
#define CRASHCONTEXT_H

#include <atomic>
#include <cstddef>

class QString;

namespace CrashContext {

constexpr std::size_t LastActionSize       = 128;
constexpr std::size_t EditorPathSize       = 1024;
constexpr std::size_t DockIdSize           = 64;
constexpr std::size_t WorkspaceRootSize    = 1024;

// Exported for direct read from the crash handler. Always NUL-terminated and
// trimmed to a UTF-8 sequence boundary.
extern char lastAction[LastActionSize];
extern char activeEditorPath[EditorPathSize];
extern char activeDockId[DockIdSize];
extern char activeWorkspaceRoot[WorkspaceRootSize];

// Monotonic ms timestamp of the last setLastAction(). Used by the report to
// answer "was the user doing something or was the app idle?".
extern std::atomic<unsigned long long> lastActionTimestampMs;

void setLastAction(const QString &name);
void setActiveEditorPath(const QString &path);
void setActiveDockId(const QString &dockObjectName);
void setActiveWorkspaceRoot(const QString &root);

} // namespace CrashContext

#endif // CRASHCONTEXT_H
