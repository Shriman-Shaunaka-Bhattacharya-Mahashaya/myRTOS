#pragma once

#include "kernel/types.hpp"
#include "kernel/tcb.hpp"

namespace myrtos {

/**
 * @brief Hardware Abstraction Layer (HAL) / Port Interface.
 * 
 * Defines architecture-dependent operations. On Windows host, implemented via Win32 Fibers.
 * On bare-metal targets, implemented via assembly SVC/PendSV context switching.
 */
class Port {
public:
    /// Initializes host fiber subsystem or target interrupt controllers.
    static Status init() noexcept;

    /// Prepares task stack frame and creates fiber context.
    static Status create_context(
        TaskControlBlock* tcb,
        TaskFunction entry_func,
        void* argument
    ) noexcept;

    /// Switches execution context from running task to newly selected task.
    static void context_switch(
        TaskControlBlock* from_tcb,
        TaskControlBlock* to_tcb
    ) noexcept;

    /// Runs on the initializing thread; returns after cooperative stop or startup failure.
    static Status start_first_task(TaskControlBlock* first_tcb) noexcept;

    /// Thread-safe stop request, handled at the next host scheduling point.
    static void request_stop() noexcept;
    static bool on_owner_thread() noexcept;

    /// Triggers voluntary CPU yield to scheduler dispatcher.
    static void yield() noexcept;

    /// Disables interrupts or enters critical section (nestable).
    static uint32_t enter_critical() noexcept;

    /// Restores interrupt state or exits critical section (nestable).
    static void exit_critical(uint32_t prev_state) noexcept;
};

/**
 * @brief RAII Critical Section Guard.
 * 
 * Guarantees exception-safe and early-return-safe restoration of interrupt state.
 */
class CriticalSectionGuard {
public:
    CriticalSectionGuard() noexcept : prev_state_(Port::enter_critical()) {}
    ~CriticalSectionGuard() noexcept { Port::exit_critical(prev_state_); }

    CriticalSectionGuard(const CriticalSectionGuard&) = delete;
    CriticalSectionGuard& operator=(const CriticalSectionGuard&) = delete;

private:
    uint32_t prev_state_;
};

} // namespace myrtos
