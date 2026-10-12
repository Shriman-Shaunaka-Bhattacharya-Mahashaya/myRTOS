#include "kernel/mutex.hpp"
#include "kernel/scheduler.hpp"
#include "port/port.hpp"

namespace myrtos {

Status Mutex::lock(TickType timeout_ticks) noexcept {
    CriticalSectionGuard guard;
    TaskControlBlock* current = Scheduler::instance().current_task();

    if (current == nullptr) {
        return Status::NotInitialized;
    }

    // 1. Mutex is currently unlocked
    if (owner_ == nullptr) {
        owner_ = current;
        original_owner_priority_ = current->current_priority;
        lock_count_ = 1;
        return Status::Ok;
    }

    // 2. Recursive acquisition by the same owner
    if (owner_ == current) {
        ++lock_count_;
        return Status::Ok;
    }

    // 3. Non-blocking request
    if (timeout_ticks == NO_WAIT) {
        return Status::WouldBlock;
    }

    // 4. Priority Inheritance Protocol (PIP):
    // If waiting task has higher priority (numerically lower) than current owner, boost owner!
    if (current->current_priority < owner_->current_priority) {
        Scheduler::instance().change_task_priority(owner_, current->current_priority);
    }

    // 5. Block waiting task until owner releases
    Scheduler::instance().block_current_task(wait_queue_, timeout_ticks);
    return current->wait_result;
}

Status Mutex::unlock() noexcept {
    CriticalSectionGuard guard;
    TaskControlBlock* current = Scheduler::instance().current_task();

    if (owner_ == nullptr || owner_ != current) {
        return Status::Error; // Cannot unlock a mutex owned by another task
    }

    // Handle nested recursive locks
    if (lock_count_ > 1) {
        --lock_count_;
        return Status::Ok;
    }

    // Mutex is completely released.
    // Restore owner priority if it was boosted via PIP
    if (owner_->current_priority != original_owner_priority_) {
        Scheduler::instance().change_task_priority(owner_, original_owner_priority_);
    }

    // Pass ownership to highest-priority waiting task
    if (!wait_queue_.empty()) {
        ListNode* node = wait_queue_.pop_front();
        auto* next_owner = static_cast<TaskControlBlock*>(node->owner);
        owner_ = next_owner;
        original_owner_priority_ = next_owner->current_priority;
        lock_count_ = 1;

        Scheduler::instance().unblock_task(next_owner, Status::Ok);
    } else {
        owner_ = nullptr;
        lock_count_ = 0;
    }

    return Status::Ok;
}

} // namespace myrtos
