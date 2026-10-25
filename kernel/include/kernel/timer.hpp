#pragma once

#include "kernel/types.hpp"
#include "kernel/list.hpp"

namespace myrtos {

using TimerCallback = void (*)(void* argument);

enum class TimerMode : uint8_t {
    OneShot = 0,
    Periodic
};

/**
 * @brief Software Timer with delta-list dispatch.
 */
class SoftwareTimer {
public:
    SoftwareTimer(
        const char* name,
        TickType period_ticks,
        TimerMode mode,
        TimerCallback callback,
        void* argument = nullptr
    ) noexcept;

    ~SoftwareTimer() noexcept { stop(); }
    SoftwareTimer(const SoftwareTimer&) = delete;
    SoftwareTimer& operator=(const SoftwareTimer&) = delete;

    /// Starts or restarts the timer.
    Status start() noexcept;

    /// Stops the timer and removes it from the active timer list.
    Status stop() noexcept;

    /// Resets the timer countdown.
    Status reset() noexcept;

    [[nodiscard]] bool is_active() const noexcept { return is_active_; }
    [[nodiscard]] TickType period() const noexcept { return period_ticks_; }
    [[nodiscard]] TickType expiry_tick() const noexcept { return expiry_tick_; }
    [[nodiscard]] const char* name() const noexcept { return name_; }

    /// Internal manager hooks
    ListNode* node() noexcept { return &node_; }
    void trigger_callback() noexcept;

private:
    friend class TimerManager;
    char name_[16]{};
    TickType period_ticks_{0};
    TickType expiry_tick_{0};
    TimerMode mode_{TimerMode::OneShot};
    TimerCallback callback_{nullptr};
    void* argument_{nullptr};
    bool is_active_{false};

    ListNode node_{this, 0};
};

/**
 * @brief Global Software Timer Manager.
 */
class TimerManager {
public:
    static TimerManager& instance() noexcept;

    TimerManager(const TimerManager&) = delete;
    TimerManager& operator=(const TimerManager&) = delete;

    /// Called by the scheduler on each SysTick.
    void process_timers(TickType current_tick) noexcept;
    void reset() noexcept;

    /// Registers/activates an active timer.
    void register_timer(SoftwareTimer* timer) noexcept;

    /// Deactivates an active timer.
    void unregister_timer(SoftwareTimer* timer) noexcept;

    [[nodiscard]] size_t active_timer_count() const noexcept { return active_timers_.size(); }

private:
    TimerManager() noexcept = default;

    IntrusiveList active_timers_{};
};

} // namespace myrtos
