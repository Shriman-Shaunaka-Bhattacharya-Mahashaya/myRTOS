#pragma once

#include "kernel/types.hpp"
#include "kernel/list.hpp"

namespace myrtos {

/**
 * @brief Counting and Binary Semaphore with deterministic priority-unblocking.
 */
class Semaphore {
public:
    constexpr Semaphore(uint32_t initial_count, uint32_t max_count) noexcept
        : count_(initial_count), max_count_(max_count) {}

    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;

    /// Attempts to acquire semaphore token with timeout. Blocks task if unavailable.
    Status acquire(TickType timeout_ticks = WAIT_FOREVER) noexcept;

    /// Non-blocking token acquisition attempt.
    Status try_acquire() noexcept {
        return acquire(NO_WAIT);
    }

    /// Releases token back to semaphore and unblocks highest priority waiting task.
    Status release() noexcept;

    /// Interrupt-safe release invoked from ISRs.
    Status release_from_isr(bool* out_higher_prio_task_woken = nullptr) noexcept;

    [[nodiscard]] uint32_t count() const noexcept { return count_; }
    [[nodiscard]] uint32_t max_count() const noexcept { return max_count_; }
    [[nodiscard]] size_t waiting_count() const noexcept { return wait_queue_.size(); }

private:
    uint32_t count_{0};
    uint32_t max_count_{1};
    IntrusiveList wait_queue_{};
};

/**
 * @brief Binary Semaphore convenience wrapper.
 */
class BinarySemaphore : public Semaphore {
public:
    constexpr explicit BinarySemaphore(bool initially_available = true) noexcept
        : Semaphore(initially_available ? 1 : 0, 1) {}
};

} // namespace myrtos
