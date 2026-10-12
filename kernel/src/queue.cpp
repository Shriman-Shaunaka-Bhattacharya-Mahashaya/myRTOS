#include "kernel/queue.hpp"
#include "kernel/scheduler.hpp"
#include "port/port.hpp"
#include <cstring>

namespace myrtos {

MessageQueue::MessageQueue(
    uint8_t* storage_buffer,
    size_t item_size,
    size_t capacity
) noexcept
    : buffer_(storage_buffer),
      item_size_(item_size),
      capacity_(capacity),
      head_(0),
      tail_(0),
      count_(0) {}

void MessageQueue::copy_item_in(const void* item_data) noexcept {
    std::memcpy(buffer_ + (tail_ * item_size_), item_data, item_size_);
    tail_ = (tail_ + 1) % capacity_;
    ++count_;
}

void MessageQueue::copy_item_out(void* item_out) noexcept {
    std::memcpy(item_out, buffer_ + (head_ * item_size_), item_size_);
    head_ = (head_ + 1) % capacity_;
    --count_;
}

Status MessageQueue::send(const void* item_data, TickType timeout_ticks) noexcept {
    if (item_data == nullptr || buffer_ == nullptr || capacity_ == 0) {
        return Status::InvalidParam;
    }

    CriticalSectionGuard guard;

    while (full()) {
        if (timeout_ticks == NO_WAIT) {
            return Status::QueueFull;
        }

        Scheduler::instance().block_current_task(send_wait_queue_, timeout_ticks);
        Status res = Scheduler::instance().current_task()->wait_result;
        if (res != Status::Ok) {
            return res; // Timeout or unblocked with error
        }
    }

    copy_item_in(item_data);

    // If any tasks were waiting to receive, wake highest priority receiver
    if (!receive_wait_queue_.empty()) {
        ListNode* node = receive_wait_queue_.pop_front();
        auto* receiver = static_cast<TaskControlBlock*>(node->owner);
        Scheduler::instance().unblock_task(receiver, Status::Ok);
    }

    return Status::Ok;
}

Status MessageQueue::receive(void* item_out, TickType timeout_ticks) noexcept {
    if (item_out == nullptr || buffer_ == nullptr || capacity_ == 0) {
        return Status::InvalidParam;
    }

    CriticalSectionGuard guard;

    while (empty()) {
        if (timeout_ticks == NO_WAIT) {
            return Status::QueueEmpty;
        }

        Scheduler::instance().block_current_task(receive_wait_queue_, timeout_ticks);
        Status res = Scheduler::instance().current_task()->wait_result;
        if (res != Status::Ok) {
            return res; // Timeout or unblocked with error
        }
    }

    copy_item_out(item_out);

    // If any tasks were waiting to send, wake highest priority sender
    if (!send_wait_queue_.empty()) {
        ListNode* node = send_wait_queue_.pop_front();
        auto* sender = static_cast<TaskControlBlock*>(node->owner);
        Scheduler::instance().unblock_task(sender, Status::Ok);
    }

    return Status::Ok;
}

Status MessageQueue::send_from_isr(const void* item_data, bool* out_higher_prio_task_woken) noexcept {
    if (item_data == nullptr || buffer_ == nullptr || capacity_ == 0) {
        return Status::InvalidParam;
    }

    CriticalSectionGuard guard;

    if (out_higher_prio_task_woken != nullptr) {
        *out_higher_prio_task_woken = false;
    }

    if (full()) {
        return Status::QueueFull;
    }

    copy_item_in(item_data);

    if (!receive_wait_queue_.empty()) {
        ListNode* node = receive_wait_queue_.pop_front();
        auto* receiver = static_cast<TaskControlBlock*>(node->owner);
        TaskControlBlock* current = Scheduler::instance().current_task();
        if (out_higher_prio_task_woken != nullptr && current != nullptr) {
            if (receiver->current_priority < current->current_priority) {
                *out_higher_prio_task_woken = true;
            }
        }
        Scheduler::instance().unblock_task(receiver, Status::Ok);
    }

    return Status::Ok;
}

Status MessageQueue::receive_from_isr(void* item_out, bool* out_higher_prio_task_woken) noexcept {
    if (item_out == nullptr || buffer_ == nullptr || capacity_ == 0) {
        return Status::InvalidParam;
    }

    CriticalSectionGuard guard;

    if (out_higher_prio_task_woken != nullptr) {
        *out_higher_prio_task_woken = false;
    }

    if (empty()) {
        return Status::QueueEmpty;
    }

    copy_item_out(item_out);

    if (!send_wait_queue_.empty()) {
        ListNode* node = send_wait_queue_.pop_front();
        auto* sender = static_cast<TaskControlBlock*>(node->owner);
        TaskControlBlock* current = Scheduler::instance().current_task();
        if (out_higher_prio_task_woken != nullptr && current != nullptr) {
            if (sender->current_priority < current->current_priority) {
                *out_higher_prio_task_woken = true;
            }
        }
        Scheduler::instance().unblock_task(sender, Status::Ok);
    }

    return Status::Ok;
}

} // namespace myrtos
