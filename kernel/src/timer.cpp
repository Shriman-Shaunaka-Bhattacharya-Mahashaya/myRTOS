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
    if (callback_ == nullptr || period_ticks_ == 0 || period_ticks_ > MAX_FINITE_WAIT) {
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
    active_timers_.push_back(timer->node());
}

void TimerManager::unregister_timer(SoftwareTimer* timer) noexcept {
    if (timer == nullptr) return;
    active_timers_.remove(timer->node());
}

void TimerManager::reset() noexcept {
    CriticalSectionGuard guard;
    while (auto* node = active_timers_.pop_front())
        static_cast<SoftwareTimer*>(node->owner)->is_active_ = false;
}
void TimerManager::process_timers(TickType current_tick) noexcept {
    CriticalSectionGuard guard;
    // Restart the search after callbacks, which may alter any timer's membership.
    while (true) {
        SoftwareTimer* due = nullptr;
        auto* node = active_timers_.head();
        for (size_t i = 0; i < active_timers_.size(); ++i, node = node->next) {
            auto* timer = static_cast<SoftwareTimer*>(node->owner);
            if (tick_reached(current_tick, timer->expiry_tick_)) { due = timer; break; }
        }
        if (!due) return;
        active_timers_.remove(due->node());
        due->is_active_ = due->mode_ == TimerMode::Periodic;
        if (due->is_active_) {
            due->expiry_tick_ = current_tick + due->period_ticks_;
            due->node_.value = due->expiry_tick_;
            active_timers_.push_back(due->node());
        }
        const auto callback = due->callback_;
        auto* argument = due->argument_;
        callback(argument); // Do not touch due afterwards: it may have been destroyed.
    }
}
} // namespace myrtos
