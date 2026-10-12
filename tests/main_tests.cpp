#include <iostream>
#include <cassert>
#include <cstring>

#include "kernel/types.hpp"
#include "kernel/list.hpp"
#include "kernel/tcb.hpp"
#include "kernel/scheduler.hpp"
#include "kernel/semaphore.hpp"
#include "kernel/mutex.hpp"
#include "kernel/queue.hpp"
#include "kernel/memory_pool.hpp"
#include "kernel/timer.hpp"
#include "hw/vic.hpp"
#include "hw/gpio.hpp"
#include "hw/uart.hpp"

using namespace myrtos;

// ----------------------------------------------------------------------------
// Test 1: Intrusive List Operations
// ----------------------------------------------------------------------------
void test_intrusive_list() {
    std::cout << "[TEST] 1. Intrusive List Operations... ";
    IntrusiveList list;
    assert(list.empty());
    assert(list.size() == 0);

    ListNode n1(nullptr, 30);
    ListNode n2(nullptr, 10);
    ListNode n3(nullptr, 20);

    list.insert_sorted_ascending(&n1);
    list.insert_sorted_ascending(&n2);
    list.insert_sorted_ascending(&n3);

    assert(list.size() == 3);
    assert(list.head()->value == 10);
    assert(list.head()->next->value == 20);
    assert(list.tail()->value == 30);

    list.remove(&n2);
    assert(list.size() == 2);
    assert(list.head()->value == 20);

    ListNode* popped = list.pop_front();
    assert(popped == &n3);
    assert(list.size() == 1);
    assert(list.head() == &n1);

    std::cout << "PASSED" << std::endl;
}

// ----------------------------------------------------------------------------
// Test 2: Stack Canary & High-Water Mark Detection
// ----------------------------------------------------------------------------
void test_stack_canary_and_watermark() {
    std::cout << "[TEST] 2. Stack Canary & Watermark Tracking... ";
    TaskControlBlock tcb;
    alignas(16) uint8_t stack_mem[2048];

    tcb.prepare_stack(stack_mem, sizeof(stack_mem));
    assert(tcb.check_stack_integrity() == true);
    assert(tcb.get_stack_high_watermark() == sizeof(stack_mem));

    // Simulate stack consumption by modifying bytes
    for (size_t i = 1000; i < sizeof(stack_mem); ++i) {
        stack_mem[i] = 0x55;
    }
    assert(tcb.check_stack_integrity() == true);
    assert(tcb.get_stack_high_watermark() == 1000);

    // Corrupt canary intentionally to verify detection
    auto* canary = reinterpret_cast<uint32_t*>(stack_mem);
    *canary = 0xBAD0BAD0;
    assert(tcb.check_stack_integrity() == false);

    std::cout << "PASSED" << std::endl;
}

// ----------------------------------------------------------------------------
// Test 3: O(1) Bitmap Priority Selection
// ----------------------------------------------------------------------------
void test_priority_bitmap_clz() {
    std::cout << "[TEST] 3. O(1) Bitmap Priority Resolution... ";
    // Priority 0 corresponds to bit 31
    // Priority 31 corresponds to bit 0
    uint32_t bitmap = 0;
    assert(count_leading_zeros(bitmap) == 32);

    bitmap |= (1U << (31U - 5)); // Priority 5
    assert(count_leading_zeros(bitmap) == 5);

    bitmap |= (1U << (31U - 2)); // Priority 2 (Higher)
    assert(count_leading_zeros(bitmap) == 2);

    bitmap |= (1U << (31U - 0)); // Priority 0 (Highest)
    assert(count_leading_zeros(bitmap) == 0);

    std::cout << "PASSED" << std::endl;
}

// ----------------------------------------------------------------------------
// Test 4: O(1) Fixed-Block Memory Pool Allocator
// ----------------------------------------------------------------------------
void test_memory_pool() {
    std::cout << "[TEST] 4. O(1) Memory Pool Allocator... ";
    alignas(void*) uint8_t pool_buffer[1024];
    constexpr size_t BLOCK_SIZE = 64;
    constexpr size_t BLOCK_COUNT = 1024 / BLOCK_SIZE;

    MemoryPool pool(pool_buffer, BLOCK_SIZE, BLOCK_COUNT);
    assert(pool.free_blocks() == BLOCK_COUNT);

    void* blocks[BLOCK_COUNT];
    for (size_t i = 0; i < BLOCK_COUNT; ++i) {
        blocks[i] = pool.allocate();
        assert(blocks[i] != nullptr);
    }
    assert(pool.free_blocks() == 0);
    assert(pool.allocate() == nullptr); // Exhausted

    // Deallocate in reverse order
    for (size_t i = 0; i < BLOCK_COUNT; ++i) {
        assert(pool.deallocate(blocks[i]) == Status::Ok);
    }
    assert(pool.free_blocks() == BLOCK_COUNT);

    // Invalid pointer checks
    uint8_t invalid_ptr[10];
    assert(pool.deallocate(invalid_ptr) == Status::InvalidParam);

    std::cout << "PASSED" << std::endl;
}

// ----------------------------------------------------------------------------
// Test 5: Message Queue Ring Buffer
// ----------------------------------------------------------------------------
void test_message_queue() {
    std::cout << "[TEST] 5. Circular Message Queue... ";
    StaticQueue<uint32_t, 4> queue;
    assert(queue.empty());
    assert(!queue.full());

    uint32_t val = 10;
    assert(queue.try_send(val) == Status::Ok);
    val = 20;
    assert(queue.try_send(val) == Status::Ok);
    val = 30;
    assert(queue.try_send(val) == Status::Ok);
    val = 40;
    assert(queue.try_send(val) == Status::Ok);

    assert(queue.full());
    val = 50;
    assert(queue.try_send(val) == Status::QueueFull); // Capacity reached

    uint32_t out_val = 0;
    assert(queue.try_receive(out_val) == Status::Ok && out_val == 10);
    assert(queue.try_receive(out_val) == Status::Ok && out_val == 20);
    assert(queue.try_receive(out_val) == Status::Ok && out_val == 30);
    assert(queue.try_receive(out_val) == Status::Ok && out_val == 40);

    assert(queue.empty());
    assert(queue.try_receive(out_val) == Status::QueueEmpty);

    std::cout << "PASSED" << std::endl;
}

// ----------------------------------------------------------------------------
// Test 6: Virtual Interrupt Controller & GPIO Simulation
// ----------------------------------------------------------------------------
void test_virtual_hardware() {
    std::cout << "[TEST] 6. Virtual Interrupt Controller & Virtual GPIO... ";
    auto& vic = hw::VirtualInterruptController::instance();
    auto& gpio = hw::VirtualGpio::instance();

    static bool gpio_isr_invoked = false;
    vic.register_handler(
        hw::IRQ_GPIO,
        [](void* /*ctx*/) {
            gpio_isr_invoked = true;
        },
        nullptr,
        3
    );
    vic.enable_irq(hw::IRQ_GPIO);

    // Configure Pin 2 as input with RisingEdge trigger
    gpio.configure_pin(2, hw::PinDirection::Input, hw::InterruptTrigger::RisingEdge);

    // Simulate input Low -> High (Rising edge)
    gpio.simulate_input_change(2, hw::PinState::High);
    assert(vic.has_pending() == true);

    // Dispatch pending IRQ
    assert(vic.dispatch_next_pending() == true);
    assert(gpio_isr_invoked == true);
    assert(vic.has_pending() == false);

    // Configure Pin 3 as Output
    gpio.configure_pin(3, hw::PinDirection::Output);
    assert(gpio.write_pin(3, hw::PinState::High) == Status::Ok);
    assert(gpio.read_pin(3) == hw::PinState::High);

    std::cout << "PASSED" << std::endl;
}

// ----------------------------------------------------------------------------
// Test 7: Virtual UART Loopback
// ----------------------------------------------------------------------------
void test_virtual_uart() {
    std::cout << "[TEST] 7. Virtual UART Loopback & Buffers... ";
    auto& uart = hw::VirtualUart::instance();

    assert(uart.write_byte('H') == Status::Ok);
    assert(uart.write_byte('i') == Status::Ok);

    char sink[16]{};
    size_t pulled = uart.pull_transmitted_data(sink, sizeof(sink));
    assert(pulled == 2);
    assert(sink[0] == 'H' && sink[1] == 'i');

    uart.simulate_incoming_data("TEST\n", 5);
    assert(uart.is_rx_available());

    uint8_t rx_byte = 0;
    assert(uart.read_byte(&rx_byte) == Status::Ok && rx_byte == 'T');
    assert(uart.read_byte(&rx_byte) == Status::Ok && rx_byte == 'E');

    std::cout << "PASSED" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "       myRTOS Kernel Verification       " << std::endl;
    std::cout << "========================================" << std::endl;

    test_intrusive_list();
    test_stack_canary_and_watermark();
    test_priority_bitmap_clz();
    test_memory_pool();
    test_message_queue();
    test_virtual_hardware();
    test_virtual_uart();

    std::cout << "========================================" << std::endl;
    std::cout << " ALL DETERMINISTIC TESTS PASSED (7/7)   " << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
