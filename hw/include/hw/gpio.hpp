#pragma once

#include "kernel/types.hpp"

namespace myrtos::hw {

constexpr size_t NUM_GPIO_PINS = 8;

enum class PinDirection : uint8_t {
    Input = 0,
    Output
};

enum class PinState : uint8_t {
    Low = 0,
    High = 1
};

enum class InterruptTrigger : uint8_t {
    None = 0,
    RisingEdge,
    FallingEdge,
    BothEdges
};

struct GpioPinConfig {
    PinDirection direction{PinDirection::Input};
    PinState state{PinState::Low};
    InterruptTrigger trigger{InterruptTrigger::None};
};

/**
 * @brief Virtual GPIO Controller.
 */
class VirtualGpio {
public:
    void reset() noexcept;
    static VirtualGpio& instance() noexcept;

    VirtualGpio(const VirtualGpio&) = delete;
    VirtualGpio& operator=(const VirtualGpio&) = delete;

    Status configure_pin(uint8_t pin, PinDirection direction, InterruptTrigger trigger = InterruptTrigger::None) noexcept;

    Status write_pin(uint8_t pin, PinState state) noexcept;

    [[nodiscard]] PinState read_pin(uint8_t pin) const noexcept;

    /// Injected by UI dashboard or simulation stimuli
    void simulate_input_change(uint8_t pin, PinState new_state) noexcept;

    [[nodiscard]] GpioPinConfig get_pin_info(uint8_t pin) const noexcept;

private:
    VirtualGpio() noexcept;

    GpioPinConfig pins_[NUM_GPIO_PINS]{};
};

} // namespace myrtos::hw
