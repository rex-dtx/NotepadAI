/*
 * NotepadADE addition: crash reporting to crash_report.txt in cwd
 * (with %APPDATA%/NotepadAI fallback).
 *
 * See CrashHandler.cpp for design notes on async-signal-safety,
 * per-section atomic writes, and the Win32 VEH + POSIX sigaltstack path.
 */

#ifndef CRASHHANDLER_H
#define CRASHHANDLER_H

namespace CrashHandler {

// Install platform crash handlers. Call as the very first thing in main()
// so faults during early initialisation still produce a report.
void install();

// Deterministic crash trigger used by scripts/test-crash-handler.sh and the
// debug-only "Help → Debug → Trigger Crash" submenu. `kind` is one of:
//   "segv"      — null pointer write (SEH ACCESS_VIOLATION / SIGSEGV)
//   "abrt"      — std::abort() (SIGABRT)
//   "sof"       — runaway recursion (SEH STACK_OVERFLOW / SIGSEGV on alt stack)
//   "terminate" — throw std::runtime_error, no catch (std::terminate path)
//   "nonstd"    — throw 42 (non-std::exception terminate path)
//   "div0"      — integer divide by zero (SEH INT_DIVIDE_BY_ZERO / SIGFPE)
// This function never returns.
[[noreturn]] void triggerCrashForTest(const char *kind);

} // namespace CrashHandler

#endif // CRASHHANDLER_H
