#pragma once

#include <cstdint>

namespace myrtos::host {
// Shared host synchronization. The lock exists before scheduler initialization.
uint32_t enter_critical() noexcept;
void exit_critical(uint32_t previous_depth) noexcept;
uint32_t suspend_critical() noexcept;
void restore_critical(uint32_t depth) noexcept;
void initialize() noexcept;
void begin() noexcept;
void finish() noexcept;
void request_stop() noexcept;
bool stop_requested() noexcept;
bool on_owner_thread() noexcept;
void poll() noexcept;
void transition(uint32_t from, uint32_t to, uint32_t tick) noexcept;
} // namespace myrtos::host
