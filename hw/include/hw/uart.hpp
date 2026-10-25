#pragma once

#include "kernel/types.hpp"

namespace myrtos::hw {

constexpr size_t UART_BUFFER_SIZE = 512;

/**
 * @brief Virtual UART Peripheral.
 */
class VirtualUart {
public:
    void reset() noexcept;
    static VirtualUart& instance() noexcept;

    VirtualUart(const VirtualUart&) = delete;
    VirtualUart& operator=(const VirtualUart&) = delete;

    /// RTOS kernel / driver writes byte to transmit
    Status write_byte(uint8_t byte) noexcept;

    /// RTOS kernel / driver reads received byte
    Status read_byte(uint8_t* out_byte) noexcept;

    /// UI / Harness injects received bytes into UART RX FIFO
    Status simulate_incoming_data(const char* data, size_t length) noexcept;

    /// UI pulls accumulated output bytes to display in the UI console
    size_t pull_transmitted_data(char* buffer_out, size_t max_len) noexcept;

    [[nodiscard]] bool is_rx_available() const noexcept;
    [[nodiscard]] bool is_tx_empty() const noexcept;

private:
    VirtualUart() noexcept = default;

    uint8_t rx_buffer_[UART_BUFFER_SIZE]{};
    size_t rx_head_{0};
    size_t rx_tail_{0};
    size_t rx_count_{0};

    uint8_t tx_buffer_[UART_BUFFER_SIZE]{};
    size_t tx_head_{0};
    size_t tx_tail_{0};
    size_t tx_count_{0};
};

} // namespace myrtos::hw
