#pragma once

#include "kernel/types.hpp"
#include "kernel/list.hpp"
#include "kernel/tcb.hpp"

namespace myrtos {

/**
 * @brief Mutex with Priority Inheritance Protocol (PIP) to prevent priority inversion.
 */
class Mutex {
public:
    constexpr Mutex() noexcept = default;

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    /// Acquires the mutex, elevating owner priority if a higher priority task blocks.
    Status lock(TickType timeout_ticks = WAIT_FOREVER) noexcept;

    /// Non-blocking acquisition attempt.
    Status try_lock() noexcept {
        return lock(NO_WAIT);
    }

    /// Releases the mutex and restores owner's base priority.
    Status unlock() noexcept;

    [[nodiscard]] TaskControlBlock* owner() const noexcept { return owner_; }
    [[nodiscard]] uint32_t lock_count() const noexcept { return lock_count_; }
    [[nodiscard]] bool is_locked() const noexcept { return owner_ != nullptr; }
    [[nodiscard]] size_t waiting_count() const noexcept { return wait_queue_.size(); }

private:
    TaskControlBlock* owner_{nullptr};
    Priority original_owner_priority_{IDLE_TASK_PRIORITY};
    uint32_t lock_count_{0};
    IntrusiveList wait_queue_{};
};

/**
 * @brief RAII Mutex Lock Guard.
 */
class MutexLockGuard {
public:
    explicit MutexLockGuard(Mutex& mtx, TickType timeout = WAIT_FOREVER) noexcept
        : mutex_(mtx), status_(mutex_.lock(timeout)) {}

    ~MutexLockGuard() noexcept {
        if (status_ == Status::Ok) {
            mutex_.unlock();
        }
    }

    [[nodiscard]] Status status() const noexcept { return status_; }
    [[nodiscard]] bool is_locked() const noexcept { return status_ == Status::Ok; }

    MutexLockGuard(const MutexLockGuard&) = delete;
    MutexLockGuard& operator=(const MutexLockGuard&) = delete;

private:
    Mutex& mutex_;
    Status status_;
};

} // namespace myrtos
