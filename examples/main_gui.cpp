#include <iostream>
#include <thread>

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

int main() {
    std::cout << "Initializing myRTOS kernel..." << std::endl;
    Status init_res = Scheduler::instance().init();
    if (init_res != Status::Ok) {
        std::cerr << "Kernel initialization failed: " << static_cast<int>(init_res) << std::endl;
        return 1;
    }

    // Configure Virtual GPIO Pin 2 as Input with RisingEdge interrupt
    hw::VirtualGpio::instance().configure_pin(2, hw::PinDirection::Input, hw::InterruptTrigger::RisingEdge);

    // Register tasks
    Scheduler::instance().create_task(
        "Producer",
        Task_Producer,
        nullptr,
        2,
        s_producer_stack,
        sizeof(s_producer_stack)
    );

    Scheduler::instance().create_task(
        "Consumer",
        Task_Consumer,
        nullptr,
        3,
        s_consumer_stack,
        sizeof(s_consumer_stack)
    );

    Scheduler::instance().create_task(
        "Heartbeat",
        Task_Heartbeat,
        nullptr,
        5,
        s_heartbeat_stack,
        sizeof(s_heartbeat_stack)
    );

    // Initialize UI Dashboard
    ui::SimulationDashboard dashboard;
    if (!dashboard.init(1280, 820, "myRTOS Interactive Simulation Dashboard")) {
        std::cerr << "Failed to initialize Dear ImGui / GLFW dashboard." << std::endl;
        return 1;
    }

    // Launch RTOS Scheduler in background host thread
    std::thread rtos_thread([]() {
        Scheduler::instance().start();
    });

    std::cout << "Starting interactive UI rendering loop..." << std::endl;
    dashboard.run();

    dashboard.shutdown();
    if (rtos_thread.joinable()) {
        rtos_thread.detach();
    }

    return 0;
}
