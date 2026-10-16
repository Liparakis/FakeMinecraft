/**
 * @file LoadStats.h
 * @brief Measures world loading: when the area around the player and the whole render
 * distance first become drawable, and how much CPU time generation and meshing took.
 *
 * Printed once per world load to stdout (prefix "[LOAD]").
 */
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace loadstats {
    using Clock = std::chrono::steady_clock;

    inline std::atomic<int64_t> generationMicros{0};
    inline std::atomic<int64_t> meshingMicros{0};
    inline std::atomic<int64_t> generatedChunks{0};
    inline std::atomic<int64_t> meshedChunks{0};

    /// Adds the lifetime of this object to a counter.
    class ScopedTimer {
    public:
        explicit ScopedTimer(std::atomic<int64_t> &target) : target(target), start(Clock::now()) {}
        ~ScopedTimer() {
            target.fetch_add(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count(),
                             std::memory_order_relaxed);
        }
    private:
        std::atomic<int64_t> &target;
        Clock::time_point start;
    };
}
