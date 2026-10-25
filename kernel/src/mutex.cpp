#include "kernel/mutex.hpp"
#include "kernel/scheduler.hpp"
#include "port/port.hpp"
#include <limits>
namespace myrtos {
void Mutex::attach(TaskControlBlock* task) noexcept {
    owner_ = task; next_held_ = task->held_mutexes; task->held_mutexes = this; lock_count_ = 1;
}
void Mutex::detach() noexcept {
    auto** link = &owner_->held_mutexes;
    while (*link && *link != this) link = &(*link)->next_held_;
    if (*link) *link = next_held_;
    next_held_ = nullptr; owner_ = nullptr; lock_count_ = 0;
}
void Mutex::recompute_priorities() noexcept {
    auto& s = Scheduler::instance();
    Priority effective[MAX_TASKS];
    for (size_t i = 0; i < s.task_count_; ++i) effective[i] = s.task_registry_[i]->base_priority;
    // Fixed-point donation handles multiple owned mutexes and transitive chains.
    for (size_t pass = 0; pass < s.task_count_; ++pass) {
        bool changed = false;
        for (size_t i = 0; i < s.task_count_; ++i) {
            auto* wait = s.task_registry_[i]->waiting_mutex;
            if (!wait || !wait->owner_) continue;
            for (size_t j = 0; j < s.task_count_; ++j)
                if (s.task_registry_[j] == wait->owner_ && effective[j] > effective[i]) {
                    effective[j] = effective[i]; changed = true;
                }
        }
        if (!changed) break;
    }
    s.lock();
    for (size_t i = 0; i < s.task_count_; ++i) s.change_task_priority(s.task_registry_[i], effective[i]);
    s.unlock();
}
Status Mutex::lock(TickType timeout) noexcept {
    CriticalSectionGuard guard;
    auto& s = Scheduler::instance();
    auto* current = s.current_task();
    if (!Port::on_owner_thread() || !current || !valid_timeout(timeout)) return Status::InvalidParam;
    if (!owner_) { attach(current); return Status::Ok; }
    if (owner_ == current) {
        if (lock_count_ == std::numeric_limits<uint32_t>::max()) return Status::ResourceExhausted;
        ++lock_count_; return Status::Ok;
    }
    if (timeout == NO_WAIT) return Status::WouldBlock;
    if (!s.can_block()) return Status::InvalidParam;
    // Reject a circular ownership dependency rather than deadlocking the session.
    for (auto* task = owner_; task; task = task->waiting_mutex ? task->waiting_mutex->owner_ : nullptr)
        if (task == current) return Status::Error;
    current->waiting_mutex = this;
    return s.block_current_task(wait_queue_, timeout);
}
Status Mutex::unlock() noexcept {
    CriticalSectionGuard guard;
    auto& s = Scheduler::instance();
    if (!Port::on_owner_thread() || !owner_ || owner_ != s.current_task()) return Status::Error;
    if (lock_count_ > 1) { --lock_count_; return Status::Ok; }
    s.lock();
    detach();
    if (auto* node = wait_queue_.pop_front()) {
        auto* next = static_cast<TaskControlBlock*>(node->owner);
        attach(next);
        s.unblock_task(next, Status::Ok);
    }
    recompute_priorities();
    s.unlock();
    return Status::Ok;
}
void Mutex::abandon_all(TaskControlBlock* task) noexcept {
    while (task->held_mutexes) task->held_mutexes->detach();
}
void Mutex::release_all(TaskControlBlock* task) noexcept {
    while (task->held_mutexes) {
        task->held_mutexes->lock_count_ = 1;
        task->held_mutexes->unlock();
    }
}
}
