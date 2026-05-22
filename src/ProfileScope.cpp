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

#ifndef NDEBUG

#include "ProfileScope.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace ProfileScope {

namespace {

struct SlotRegistry {
    std::mutex mtx;
    std::unordered_map<std::string, std::unique_ptr<Slot>> byTag;
};

SlotRegistry &registry()
{
    static SlotRegistry r;
    return r;
}

} // namespace

Scope::Scope(std::atomic<uint64_t> *callCount,
             std::atomic<uint64_t> *totalNs,
             std::atomic<uint64_t> *maxNs)
    : m_callCount(callCount)
    , m_totalNs(totalNs)
    , m_maxNs(maxNs)
    , m_start(std::chrono::steady_clock::now())
{}

Scope::~Scope()
{
    const auto end = std::chrono::steady_clock::now();
    const uint64_t ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - m_start).count());

    m_callCount->fetch_add(1, std::memory_order_relaxed);
    m_totalNs->fetch_add(ns, std::memory_order_relaxed);

    // Update max via CAS loop (lock-free).
    uint64_t prev = m_maxNs->load(std::memory_order_relaxed);
    while (ns > prev &&
           !m_maxNs->compare_exchange_weak(prev, ns,
                                           std::memory_order_relaxed,
                                           std::memory_order_relaxed)) {
        // prev was updated by CAS; retry.
    }
}

Slot *acquireSlot(const char *tag)
{
    auto &r = registry();
    std::lock_guard<std::mutex> lock(r.mtx);

    const std::string key(tag);
    auto it = r.byTag.find(key);
    if (it != r.byTag.end()) {
        return it->second.get();
    }

    auto slot = std::make_unique<Slot>();
    slot->tag = tag;
    Slot *raw = slot.get();
    r.byTag.emplace(key, std::move(slot));
    return raw;
}

std::vector<Sample> snapshot()
{
    auto &r = registry();
    std::lock_guard<std::mutex> lock(r.mtx);

    std::vector<Sample> out;
    out.reserve(r.byTag.size());

    for (const auto &kv : r.byTag) {
        Sample s;
        s.tag = QString::fromUtf8(kv.first.c_str());
        s.callCount = kv.second->callCount.load(std::memory_order_relaxed);
        s.totalNs = kv.second->totalNs.load(std::memory_order_relaxed);
        s.maxNs = kv.second->maxNs.load(std::memory_order_relaxed);
        if (s.callCount > 0) {
            out.push_back(std::move(s));
        }
    }

    std::sort(out.begin(), out.end(), [](const Sample &a, const Sample &b) {
        return a.totalNs > b.totalNs;
    });

    return out;
}

void resetForTesting()
{
    auto &r = registry();
    std::lock_guard<std::mutex> lock(r.mtx);
    // Do NOT erase slots: existing PROFILE_SCOPE call sites cache the Slot*
    // in a static-local, so erasing would dangle those pointers across test
    // methods. Zero the counters instead.
    for (auto &kv : r.byTag) {
        kv.second->callCount.store(0, std::memory_order_relaxed);
        kv.second->totalNs.store(0, std::memory_order_relaxed);
        kv.second->maxNs.store(0, std::memory_order_relaxed);
    }
}

} // namespace ProfileScope

#endif // NDEBUG
