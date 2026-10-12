#include "hw/gpio.hpp"
#include "hw/vic.hpp"

namespace myrtos::hw {

VirtualGpio& VirtualGpio::instance() noexcept {
    static VirtualGpio s_instance;
    return s_instance;
}

VirtualGpio::VirtualGpio() noexcept {
    for (size_t i = 0; i < NUM_GPIO_PINS; ++i) {
        pins_[i].direction = PinDirection::Input;
        pins_[i].state = PinState::Low;
        pins_[i].trigger = InterruptTrigger::None;
    }
}

Status VirtualGpio::configure_pin(uint8_t pin, PinDirection direction, InterruptTrigger trigger) noexcept {
    if (pin >= NUM_GPIO_PINS) {
        return Status::InvalidParam;
    }

    pins_[pin].direction = direction;
    pins_[pin].trigger = trigger;
    return Status::Ok;
}

Status VirtualGpio::write_pin(uint8_t pin, PinState state) noexcept {
    if (pin >= NUM_GPIO_PINS) {
        return Status::InvalidParam;
    }

    if (pins_[pin].direction != PinDirection::Output) {
        return Status::Error; // Cannot write to input pin
    }

    pins_[pin].state = state;
    return Status::Ok;
}

PinState VirtualGpio::read_pin(uint8_t pin) const noexcept {
    if (pin >= NUM_GPIO_PINS) {
        return PinState::Low;
    }
    return pins_[pin].state;
}

void VirtualGpio::simulate_input_change(uint8_t pin, PinState new_state) noexcept {
    if (pin >= NUM_GPIO_PINS) return;

    PinState old_state = pins_[pin].state;
    pins_[pin].state = new_state;

    if (old_state != new_state) {
        bool trigger_interrupt = false;
        switch (pins_[pin].trigger) {
            case InterruptTrigger::RisingEdge:
                trigger_interrupt = (old_state == PinState::Low && new_state == PinState::High);
                break;
            case InterruptTrigger::FallingEdge:
                trigger_interrupt = (old_state == PinState::High && new_state == PinState::Low);
                break;
            case InterruptTrigger::BothEdges:
                trigger_interrupt = true;
                break;
            case InterruptTrigger::None:
            default:
                break;
        }

        if (trigger_interrupt) {
            VirtualInterruptController::instance().trigger_irq(IRQ_GPIO);
        }
    }
}

const GpioPinConfig& VirtualGpio::get_pin_info(uint8_t pin) const noexcept {
    static GpioPinConfig invalid_pin{};
    if (pin >= NUM_GPIO_PINS) {
        return invalid_pin;
    }
    return pins_[pin];
}

} // namespace myrtos::hw
