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

#ifndef PROFILESCOPE_H
#define PROFILESCOPE_H

#ifndef NDEBUG

#include <atomic>
#include <chrono>
#include <cstdint>
#include <vector>

#include <QString>

namespace ProfileScope {

struct Sample {
    QString tag;
    quint64 callCount = 0;
    quint64 totalNs = 0;
    quint64 maxNs = 0;
};

// Aggregate per-tag statistics. Tags are interned into a global table the first
// time PROFILE_SCOPE is invoked at a particular call site, so subsequent hits
// hash to a stable bucket without re-locking.
class Scope {
public:
    Scope(std::atomic<uint64_t> *callCount,
          std::atomic<uint64_t> *totalNs,
          std::atomic<uint64_t> *maxNs);
    ~Scope();

    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

private:
    std::atomic<uint64_t> *m_callCount;
    std::atomic<uint64_t> *m_totalNs;
    std::atomic<uint64_t> *m_maxNs;
    std::chrono::steady_clock::time_point m_start;
};

struct Slot {
    const char *tag;
    std::atomic<uint64_t> callCount{0};
    std::atomic<uint64_t> totalNs{0};
    std::atomic<uint64_t> maxNs{0};
};

// Returns a stable Slot for a given compile-time tag. Lock taken once on
// first call; subsequent calls return the cached pointer.
Slot *acquireSlot(const char *tag);

// Returns a snapshot of all slots, sorted by totalNs descending.
std::vector<Sample> snapshot();

// Test helper — clears all slots. Not called from production code.
void resetForTesting();

} // namespace ProfileScope

#define PROFILE_SCOPE_CAT_INNER(a, b) a##b
#define PROFILE_SCOPE_CAT(a, b) PROFILE_SCOPE_CAT_INNER(a, b)

#define PROFILE_SCOPE(tag) \
    static ::ProfileScope::Slot *PROFILE_SCOPE_CAT(_ps_slot_, __LINE__) = ::ProfileScope::acquireSlot(tag); \
    ::ProfileScope::Scope PROFILE_SCOPE_CAT(_ps_scope_, __LINE__)( \
        &PROFILE_SCOPE_CAT(_ps_slot_, __LINE__)->callCount, \
        &PROFILE_SCOPE_CAT(_ps_slot_, __LINE__)->totalNs, \
        &PROFILE_SCOPE_CAT(_ps_slot_, __LINE__)->maxNs)

#else // NDEBUG

#define PROFILE_SCOPE(tag) ((void)0)

#endif // NDEBUG

#endif // PROFILESCOPE_H
