#pragma once

#include <cstdint>
#include <cstddef>
#include <bit>

namespace myrtos {

/// System tick representation (millisecond resolution standard)
using TickType = uint32_t;

/// Task priority (0 = highest priority, 31 = lowest priority / Idle)
using Priority = uint8_t;

/// Unique identifier for tasks
using TaskId = uint32_t;

/// Standard RTOS Priority limits
constexpr Priority MAX_PRIORITIES = 32;
constexpr Priority HIGHEST_PRIORITY = 0;
constexpr Priority LOWEST_PRIORITY = 31;
constexpr Priority IDLE_TASK_PRIORITY = LOWEST_PRIORITY;

/// Timeout and Sentinel constants
constexpr TickType WAIT_FOREVER = 0xFFFFFFFFU;
constexpr TickType NO_WAIT = 0U;
constexpr TaskId INVALID_TASK_ID = 0xFFFFFFFFU;

/// Minimum and default task stack sizes (in bytes)
constexpr size_t MIN_STACK_SIZE_BYTES = 1024;
constexpr size_t DEFAULT_STACK_SIZE_BYTES = 4096;

/// Stack canary magic pattern for overflow detection
constexpr uint32_t STACK_CANARY_PATTERN = 0xDEADBEEFU;

/// Task lifecycle states
enum class TaskState : uint8_t {
    Ready = 0,
    Running,
    Blocked,
    Suspended,
    Terminated
};

/// Universal RTOS status and error codes
enum class Status : int32_t {
    Ok = 0,
    Error = -1,
    Timeout = -2,
    ResourceExhausted = -3,
    InvalidParam = -4,
    WouldBlock = -5,
    NotInitialized = -6,
    NotFound = -7,
    QueueFull = -8,
    QueueEmpty = -9,
    StackOverflow = -10
};

/// Portable C++20 bit scan; zero has 32 leading zeros.
inline constexpr uint32_t count_leading_zeros(uint32_t value) noexcept {
    return static_cast<uint32_t>(std::countl_zero(value));
}

} // namespace myrtos
