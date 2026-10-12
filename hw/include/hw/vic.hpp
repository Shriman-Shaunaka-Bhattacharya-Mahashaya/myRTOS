#pragma once

#include "kernel/types.hpp"

namespace myrtos::hw {

constexpr size_t NUM_IRQS = 16;

constexpr uint32_t IRQ_SYSTICK = 0;
constexpr uint32_t IRQ_UART_RX = 1;
constexpr uint32_t IRQ_UART_TX = 2;
constexpr uint32_t IRQ_GPIO    = 3;

using IsrHandler = void (*)(void* context);

struct IrqLine {
    IsrHandler handler{nullptr};
    void* context{nullptr};
    uint8_t priority{15};
    bool enabled{false};
    bool pending{false};
};

/**
 * @brief Virtual Interrupt Controller (VIC)
 * 
 * Simulates a priority-vectored hardware interrupt controller.
 */
class VirtualInterruptController {
public:
    static VirtualInterruptController& instance() noexcept;

    VirtualInterruptController(const VirtualInterruptController&) = delete;
    VirtualInterruptController& operator=(const VirtualInterruptController&) = delete;

    /// Registers an ISR handler for a given IRQ line.
    Status register_handler(uint32_t irq, IsrHandler handler, void* context = nullptr, uint8_t priority = 7) noexcept;

    /// Enables an interrupt line.
    void enable_irq(uint32_t irq) noexcept;

    /// Disables an interrupt line.
    void disable_irq(uint32_t irq) noexcept;

    /// Triggers an interrupt (sets pending flag). Thread-safe.
    void trigger_irq(uint32_t irq) noexcept;

    /// Clears a pending interrupt.
    void clear_pending(uint32_t irq) noexcept;

    /// Evaluates and executes the highest-priority pending interrupt.
    /// Returns true if an ISR was dispatched.
    bool dispatch_next_pending() noexcept;

    /// Checks if any enabled interrupt is currently pending.
    [[nodiscard]] bool has_pending() const noexcept;

private:
    VirtualInterruptController() noexcept = default;

    IrqLine lines_[NUM_IRQS]{};
};

} // namespace myrtos::hw
