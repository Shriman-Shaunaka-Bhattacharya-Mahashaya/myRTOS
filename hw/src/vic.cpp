#include "port/port.hpp"
#include "hw/vic.hpp"

namespace myrtos::hw {

VirtualInterruptController& VirtualInterruptController::instance() noexcept {
    static VirtualInterruptController s_instance;
    return s_instance;
}

Status VirtualInterruptController::register_handler(
    uint32_t irq,
    IsrHandler handler,
    void* context,
    uint8_t priority
) noexcept {
    CriticalSectionGuard guard;
    if (irq >= NUM_IRQS || handler == nullptr) {
        return Status::InvalidParam;
    }

    lines_[irq].handler = handler;
    lines_[irq].context = context;
    lines_[irq].priority = priority;
    lines_[irq].enabled = false;
    lines_[irq].pending = false;

    return Status::Ok;
}

void VirtualInterruptController::enable_irq(uint32_t irq) noexcept {
    CriticalSectionGuard guard;
    if (irq < NUM_IRQS) {
        lines_[irq].enabled = true;
    }
}

void VirtualInterruptController::disable_irq(uint32_t irq) noexcept {
    CriticalSectionGuard guard;
    if (irq < NUM_IRQS) {
        lines_[irq].enabled = false;
    }
}

void VirtualInterruptController::trigger_irq(uint32_t irq) noexcept {
    CriticalSectionGuard guard;
    if (irq < NUM_IRQS && lines_[irq].enabled) {
        lines_[irq].pending = true;
    }
}

void VirtualInterruptController::clear_pending(uint32_t irq) noexcept {
    CriticalSectionGuard guard;
    if (irq < NUM_IRQS) {
        lines_[irq].pending = false;
    }
}

bool VirtualInterruptController::has_pending() const noexcept {
    CriticalSectionGuard guard;
    for (size_t i = 0; i < NUM_IRQS; ++i) {
        if (lines_[i].enabled && lines_[i].pending) {
            return true;
        }
    }
    return false;
}

bool VirtualInterruptController::dispatch_next_pending() noexcept {
    CriticalSectionGuard guard;
    int32_t best_irq = -1;
    uint8_t highest_priority = 255;

    for (size_t i = 0; i < NUM_IRQS; ++i) {
        if (lines_[i].enabled && lines_[i].pending) {
            if (lines_[i].priority < highest_priority) {
                highest_priority = lines_[i].priority;
                best_irq = static_cast<int32_t>(i);
            }
        }
    }

    if (best_irq >= 0 && lines_[best_irq].handler != nullptr) {
        lines_[best_irq].pending = false;
        lines_[best_irq].handler(lines_[best_irq].context);
        return true;
    }

    return false;
}

} // namespace myrtos::hw
