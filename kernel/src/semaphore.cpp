#include "kernel/semaphore.hpp"
#include "kernel/scheduler.hpp"
#include "port/port.hpp"

namespace myrtos {

Status Semaphore::acquire(TickType timeout_ticks) noexcept {
    CriticalSectionGuard guard;

    if (count_ > 0) {
        --count_;
        return Status::Ok;
    }

    if (timeout_ticks == NO_WAIT) {
        return Status::WouldBlock;
    }

    Scheduler::instance().block_current_task(wait_queue_, timeout_ticks);
    return Scheduler::instance().current_task()->wait_result;
}

Status Semaphore::release() noexcept {
    CriticalSectionGuard guard;

    if (!wait_queue_.empty()) {
        ListNode* node = wait_queue_.pop_front();
        auto* waiting_task = static_cast<TaskControlBlock*>(node->owner);
        Scheduler::instance().unblock_task(waiting_task, Status::Ok);
        return Status::Ok;
    }

    if (count_ < max_count_) {
        ++count_;
        return Status::Ok;
    }

    return Status::ResourceExhausted;
}

Status Semaphore::release_from_isr(bool* out_higher_prio_task_woken) noexcept {
    CriticalSectionGuard guard;

    if (out_higher_prio_task_woken != nullptr) {
        *out_higher_prio_task_woken = false;
    }

    if (!wait_queue_.empty()) {
        ListNode* node = wait_queue_.pop_front();
        auto* waiting_task = static_cast<TaskControlBlock*>(node->owner);
        
        TaskControlBlock* current = Scheduler::instance().current_task();
        if (out_higher_prio_task_woken != nullptr && current != nullptr) {
            if (waiting_task->current_priority < current->current_priority) {
                *out_higher_prio_task_woken = true;
            }
        }

        Scheduler::instance().unblock_task(waiting_task, Status::Ok);
        return Status::Ok;
    }

    if (count_ < max_count_) {
        ++count_;
        return Status::Ok;
    }

    return Status::ResourceExhausted;
}

} // namespace myrtos
