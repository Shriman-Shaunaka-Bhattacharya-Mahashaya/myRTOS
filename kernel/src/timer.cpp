#include "kernel/timer.hpp"
#include "kernel/scheduler.hpp"
#include "port/port.hpp"
#include <cstring>

namespace myrtos {

SoftwareTimer::SoftwareTimer(
    const char* name,
    TickType period_ticks,
    TimerMode mode,
    TimerCallback callback,
    void* argument
) noexcept
    : period_ticks_(period_ticks),
      expiry_tick_(0),
      mode_(mode),
      callback_(callback),
      argument_(argument),
      is_active_(false),
      node_(this, 0) {
    std::strncpy(name_, name ? name : "tmr", sizeof(name_) - 1);
    name_[sizeof(name_) - 1] = '\0';
}

Status SoftwareTimer::start() noexcept {
    CriticalSectionGuard guard;
    if (callback_ == nullptr || period_ticks_ == 0) {
        return Status::InvalidParam;
    }

    if (is_active_) {
        TimerManager::instance().unregister_timer(this);
    }

    expiry_tick_ = Scheduler::instance().tick_count() + period_ticks_;
    node_.value = expiry_tick_;
    is_active_ = true;

    TimerManager::instance().register_timer(this);
    return Status::Ok;
}

Status SoftwareTimer::stop() noexcept {
    CriticalSectionGuard guard;
    if (!is_active_) {
        return Status::Ok;
    }

    TimerManager::instance().unregister_timer(this);
    is_active_ = false;
    return Status::Ok;
}

Status SoftwareTimer::reset() noexcept {
    return start();
}

void SoftwareTimer::trigger_callback() noexcept {
    if (callback_ != nullptr) {
        callback_(argument_);
    }
}

// ----------------------------------------------------------------------------
// TimerManager Implementation
// ----------------------------------------------------------------------------

TimerManager& TimerManager::instance() noexcept {
    static TimerManager s_instance;
    return s_instance;
}

void TimerManager::register_timer(SoftwareTimer* timer) noexcept {
    if (timer == nullptr) return;
    active_timers_.insert_sorted_ascending(timer->node());
}

void TimerManager::unregister_timer(SoftwareTimer* timer) noexcept {
    if (timer == nullptr) return;
    active_timers_.remove(timer->node());
}

void TimerManager::process_timers(TickType current_tick) noexcept {
    while (!active_timers_.empty()) {
        ListNode* head = active_timers_.head();
        if (head->value <= current_tick) {
            active_timers_.remove(head);
            auto* timer = static_cast<SoftwareTimer*>(head->owner);
            timer->trigger_callback();

            if (timer->is_active()) {
                // If periodic, schedule next expiration
                timer->start();
            }
        } else {
            // Since active_timers_ is sorted ascending, earliest timer is in the future
            break;
        }
    }
}

} // namespace myrtos
