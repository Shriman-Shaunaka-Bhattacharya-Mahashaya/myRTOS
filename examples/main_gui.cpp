#include <iostream>
#include <thread>
#include <future>
#include <cstring>
#include "port/port.hpp"

#include "kernel/types.hpp"
#include "kernel/scheduler.hpp"
#include "kernel/semaphore.hpp"
#include "kernel/queue.hpp"
#include "hw/gpio.hpp"
#include "hw/uart.hpp"
#include "ui/dashboard.hpp"

using namespace myrtos;

struct SensorReading {
    uint32_t timestamp;
    uint16_t raw_value;
};

// Statically allocated task stacks
alignas(16) static uint8_t s_producer_stack[DEFAULT_STACK_SIZE_BYTES];
alignas(16) static uint8_t s_consumer_stack[DEFAULT_STACK_SIZE_BYTES];
alignas(16) static uint8_t s_heartbeat_stack[DEFAULT_STACK_SIZE_BYTES];

static StaticQueue<SensorReading, 16> s_sensor_queue;
static BinarySemaphore s_data_ready_sem(false);

// Task 1: Producer (High Priority)
static void Task_Producer(void* /*arg*/) {
    uint16_t sample_counter = 100;
    while (true) {
        SensorReading reading{
            Scheduler::instance().tick_count(),
            sample_counter++
        };

        s_sensor_queue.try_send(reading);
        s_data_ready_sem.release();

        Scheduler::instance().sleep_ticks(150); // 150 ms sample rate
    }
}

// Task 2: Consumer (Medium Priority)
static void Task_Consumer(void* /*arg*/) {
    while (true) {
        if (s_data_ready_sem.acquire(WAIT_FOREVER) == Status::Ok) {
            SensorReading reading{};
            if (s_sensor_queue.receive(reading, NO_WAIT) == Status::Ok) {
                char msg[64];
                int len = std::snprintf(msg, sizeof(msg), "[SENSOR] Tick=%u Val=%u\n", 
                                        reading.timestamp, reading.raw_value);
                for (int i = 0; i < len; ++i) {
                    hw::VirtualUart::instance().write_byte(static_cast<uint8_t>(msg[i]));
                }
            }
        }
    }
}

// Task 3: Heartbeat (Low Priority)
static void Task_Heartbeat(void* /*arg*/) {
    bool led_state = false;
    hw::VirtualGpio::instance().configure_pin(0, hw::PinDirection::Output);

    while (true) {
        led_state = !led_state;
        hw::VirtualGpio::instance().write_pin(
            0, led_state ? hw::PinState::High : hw::PinState::Low
        );

        Scheduler::instance().sleep_ticks(300); // 300 ms LED toggle
    }
}

static Status setup_simulation() {
    auto& scheduler = Scheduler::instance();
    auto status = scheduler.init();
    if (status != Status::Ok) return status;
    hw::VirtualGpio::instance().configure_pin(2, hw::PinDirection::Input, hw::InterruptTrigger::RisingEdge);
    status = scheduler.create_task("Producer", Task_Producer, nullptr, 2, s_producer_stack, sizeof(s_producer_stack));
    if (status != Status::Ok) return status;
    status = scheduler.create_task("Consumer", Task_Consumer, nullptr, 3, s_consumer_stack, sizeof(s_consumer_stack));
    if (status != Status::Ok) return status;
    return scheduler.create_task("Heartbeat", Task_Heartbeat, nullptr, 5, s_heartbeat_stack, sizeof(s_heartbeat_stack));
}

int main(int argc, char** argv) {
    const bool smoke_test = argc == 2 && std::strcmp(argv[1], "--smoke-test") == 0;
    if (argc > 1 && !smoke_test) {
        std::cerr << "Usage: myrtos_dashboard [--smoke-test]\n";
        return 2;
    }
    ui::SimulationDashboard dashboard;
    if (!dashboard.init()) return 1;

    // Initialize and run contexts on the same host thread on both platforms.
    std::promise<Status> initialized;
    auto ready = initialized.get_future();
    Status run_result = Status::Error;
    std::thread rtos_thread([&]() {
        const auto status = setup_simulation();
        initialized.set_value(status);
        if (status == Status::Ok) run_result = Scheduler::instance().start();
    });
    const auto status = ready.get();
    if (status != Status::Ok) {
        rtos_thread.join();
        std::cerr << "Simulation initialization failed: " << static_cast<int>(status) << '\n';
        return 1;
    }

    dashboard.run(smoke_test ? 120 : 0);
    Port::request_stop();
    rtos_thread.join();
    dashboard.shutdown();
    if (run_result != Status::Ok) return 1;
    if (smoke_test) {
        if (Scheduler::instance().tick_count() == 0) return 1;
        std::cout << "Dashboard smoke test: 120 frames, advancing clock, clean shutdown.\n";
    }
    return 0;
}
