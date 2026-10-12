#pragma once

#include "kernel/types.hpp"
#include "kernel/list.hpp"

namespace myrtos {

using TaskFunction = void (*)(void* param);

constexpr size_t TASK_NAME_MAX_LEN = 16;
constexpr uint8_t STACK_FILL_BYTE = 0xA5;

/**
 * @brief Task Control Block (TCB)
 * 
 * Represents the complete state and execution context of an RTOS task.
 * Designed for zero dynamic memory allocation and O(1) list transitions.
 */
struct TaskControlBlock {
    TaskId id{INVALID_TASK_ID};
    char name[TASK_NAME_MAX_LEN]{};
    TaskState state{TaskState::Ready};

    Priority base_priority{IDLE_TASK_PRIORITY};
    Priority current_priority{IDLE_TASK_PRIORITY}; // Dynamic for Priority Inheritance Protocol

    void* stack_ptr{nullptr};       // Active stack pointer upon context switch
    uint8_t* stack_base{nullptr};   // Lowest address of allocated stack buffer
    size_t stack_size{0};           // Total stack size in bytes

    TickType wake_tick{0};          // Absolute tick when sleep/timeout expires
    Status wait_result{Status::Ok}; // Result status when awakened from block

    // Intrusive list nodes for O(1) state transitions
    ListNode state_node{this, 0};   // Linked into Ready, Delayed, or Suspended lists
    ListNode event_node{this, 0};   // Linked into Mutex, Semaphore, or Queue wait lists

    void* port_context{nullptr};    // Architecture-specific context (e.g. Win32 Fiber handle)

    // Telemetry & Diagnostic instrumentation
    uint64_t runtime_ticks{0};
    uint32_t context_switch_count{0};

    constexpr TaskControlBlock() noexcept = default;

    /**
     * @brief Formats the stack with fill bytes and places stack canaries.
     */
    void prepare_stack(uint8_t* stack_memory, size_t size_bytes) noexcept {
        stack_base = stack_memory;
        stack_size = size_bytes;

        // Fill stack with diagnostic watermark pattern
        for (size_t i = 0; i < size_bytes; ++i) {
            stack_base[i] = STACK_FILL_BYTE;
        }

        // Place canary at the bottom (stack grows downward on x86/ARM)
        auto* canary = reinterpret_cast<uint32_t*>(stack_base);
        *canary = STACK_CANARY_PATTERN;
    }

    /**
     * @brief Verifies that the stack canary has not been overwritten.
     * @return true if stack is healthy, false if stack overflow detected.
     */
    [[nodiscard]] bool check_stack_integrity() const noexcept {
        if (stack_base == nullptr) return false;
        const auto* canary = reinterpret_cast<const uint32_t*>(stack_base);
        return *canary == STACK_CANARY_PATTERN;
    }

    /**
     * @brief Computes remaining unused stack memory (in bytes) above the canary.
     */
    [[nodiscard]] size_t get_stack_high_watermark() const noexcept {
        if (stack_base == nullptr || stack_size <= sizeof(uint32_t)) return 0;

        size_t free_bytes = sizeof(uint32_t); // Skip canary
        while (free_bytes < stack_size && stack_base[free_bytes] == STACK_FILL_BYTE) {
            ++free_bytes;
        }
        return free_bytes;
    }

    /**
     * @brief Returns current stack utilization percentage [0.0, 100.0].
     */
    [[nodiscard]] float get_stack_usage_percentage() const noexcept {
        if (stack_size == 0) return 0.0f;
        size_t unused = get_stack_high_watermark();
        return (static_cast<float>(stack_size - unused) / static_cast<float>(stack_size)) * 100.0f;
    }
};

} // namespace myrtos
