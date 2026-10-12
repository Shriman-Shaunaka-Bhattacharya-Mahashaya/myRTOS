#pragma once

#include "kernel/types.hpp"
#include "kernel/list.hpp"

namespace myrtos {

/**
 * @brief Raw deterministic message queue operating on raw memory buffers.
 */
class MessageQueue {
public:
    MessageQueue(
        uint8_t* storage_buffer,
        size_t item_size,
        size_t capacity
    ) noexcept;

    MessageQueue(const MessageQueue&) = delete;
    MessageQueue& operator=(const MessageQueue&) = delete;

    /// Sends an item to the back of the queue. Blocks if full until timeout.
    Status send(const void* item_data, TickType timeout_ticks = WAIT_FOREVER) noexcept;

    /// Receives an item from the front of the queue. Blocks if empty until timeout.
    Status receive(void* item_out, TickType timeout_ticks = WAIT_FOREVER) noexcept;

    /// Non-blocking send
    Status try_send(const void* item_data) noexcept {
        return send(item_data, NO_WAIT);
    }

    /// Non-blocking receive
    Status try_receive(void* item_out) noexcept {
        return receive(item_out, NO_WAIT);
    }

    /// Interrupt-safe send
    Status send_from_isr(const void* item_data, bool* out_higher_prio_task_woken = nullptr) noexcept;

    /// Interrupt-safe receive
    Status receive_from_isr(void* item_out, bool* out_higher_prio_task_woken = nullptr) noexcept;

    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }
    [[nodiscard]] bool full() const noexcept { return count_ == capacity_; }
    [[nodiscard]] size_t count() const noexcept { return count_; }
    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] size_t item_size() const noexcept { return item_size_; }

private:
    void copy_item_in(const void* item_data) noexcept;
    void copy_item_out(void* item_out) noexcept;

    uint8_t* buffer_{nullptr};
    size_t item_size_{0};
    size_t capacity_{0};
    size_t head_{0};
    size_t tail_{0};
    size_t count_{0};

    IntrusiveList send_wait_queue_{};
    IntrusiveList receive_wait_queue_{};
};

/**
 * @brief Strongly-typed statically allocated Message Queue.
 */
template <typename T, size_t Capacity>
class StaticQueue {
public:
    StaticQueue() noexcept
        : raw_queue_(reinterpret_cast<uint8_t*>(storage_), sizeof(T), Capacity) {}

    Status send(const T& item, TickType timeout = WAIT_FOREVER) noexcept {
        return raw_queue_.send(&item, timeout);
    }

    Status try_send(const T& item) noexcept {
        return raw_queue_.try_send(&item);
    }

    Status receive(T& item_out, TickType timeout = WAIT_FOREVER) noexcept {
        return raw_queue_.receive(&item_out, timeout);
    }

    Status try_receive(T& item_out) noexcept {
        return raw_queue_.try_receive(&item_out);
    }

    Status send_from_isr(const T& item, bool* woken = nullptr) noexcept {
        return raw_queue_.send_from_isr(&item, woken);
    }

    Status receive_from_isr(T& item_out, bool* woken = nullptr) noexcept {
        return raw_queue_.receive_from_isr(&item_out, woken);
    }

    [[nodiscard]] bool empty() const noexcept { return raw_queue_.empty(); }
    [[nodiscard]] bool full() const noexcept { return raw_queue_.full(); }
    [[nodiscard]] size_t count() const noexcept { return raw_queue_.count(); }

private:
    alignas(T) uint8_t storage_[sizeof(T) * Capacity];
    MessageQueue raw_queue_;
};

} // namespace myrtos
