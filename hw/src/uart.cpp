#include "port/port.hpp"
#include "hw/uart.hpp"
#include "hw/vic.hpp"

namespace myrtos::hw {

VirtualUart& VirtualUart::instance() noexcept {
    static VirtualUart s_instance;
    return s_instance;
}

Status VirtualUart::write_byte(uint8_t byte) noexcept {
    CriticalSectionGuard guard;
    if (tx_count_ >= UART_BUFFER_SIZE) {
        return Status::ResourceExhausted;
    }

    tx_buffer_[tx_tail_] = byte;
    tx_tail_ = (tx_tail_ + 1) % UART_BUFFER_SIZE;
    ++tx_count_;

    return Status::Ok;
}

Status VirtualUart::read_byte(uint8_t* out_byte) noexcept {
    CriticalSectionGuard guard;
    if (out_byte == nullptr || rx_count_ == 0) {
        return Status::QueueEmpty;
    }

    *out_byte = rx_buffer_[rx_head_];
    rx_head_ = (rx_head_ + 1) % UART_BUFFER_SIZE;
    --rx_count_;

    return Status::Ok;
}

void VirtualUart::simulate_incoming_data(const char* data, size_t length) noexcept {
    CriticalSectionGuard guard;
    if (data == nullptr || length == 0) return;

    for (size_t i = 0; i < length && rx_count_ < UART_BUFFER_SIZE; ++i) {
        rx_buffer_[rx_tail_] = static_cast<uint8_t>(data[i]);
        rx_tail_ = (rx_tail_ + 1) % UART_BUFFER_SIZE;
        ++rx_count_;
    }

    // Trigger Virtual UART RX Interrupt
    VirtualInterruptController::instance().trigger_irq(IRQ_UART_RX);
}

size_t VirtualUart::pull_transmitted_data(char* buffer_out, size_t max_len) noexcept {
    CriticalSectionGuard guard;
    if (buffer_out == nullptr || max_len == 0 || tx_count_ == 0) {
        return 0;
    }

    size_t copied = 0;
    while (copied < max_len && tx_count_ > 0) {
        buffer_out[copied++] = static_cast<char>(tx_buffer_[tx_head_]);
        tx_head_ = (tx_head_ + 1) % UART_BUFFER_SIZE;
        --tx_count_;
    }
    return copied;
}

bool VirtualUart::is_rx_available() const noexcept {
    CriticalSectionGuard guard;
    return rx_count_ > 0;
}

bool VirtualUart::is_tx_empty() const noexcept {
    CriticalSectionGuard guard;
    return tx_count_ == 0;
}

} // namespace myrtos::hw
