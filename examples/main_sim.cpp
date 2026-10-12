#include <iostream>
#include <cstdio>

#include "kernel/types.hpp"
#include "kernel/scheduler.hpp"
#include "kernel/semaphore.hpp"
#include "kernel/queue.hpp"
#include "kernel/timer.hpp"
#include "hw/gpio.hpp"
#include "hw/uart.hpp"

using namespace myrtos;

struct SensorReading {
    uint32_t timestamp;
    uint16_t raw_value;
};

// Static storage allocations (Zero runtime heap allocations)
alignas(16) static uint8_t s_producer_stack[DEFAULT_STACK_SIZE_BYTES];
alignas(16) static uint8_t s_consumer_stack[DEFAULT_STACK_SIZE_BYTES];
alignas(16) static uint8_t s_heartbeat_stack[DEFAULT_STACK_SIZE_BYTES];

static StaticQueue<SensorReading, 8> s_sensor_queue;
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

        Scheduler::instance().sleep_ticks(200); // 200 ms sample period
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

// Task 3: Heartbeat & Diagnostics (Low Priority)
static void Task_Heartbeat(void* /*arg*/) {
    bool led_state = false;
    hw::VirtualGpio::instance().configure_pin(0, hw::PinDirection::Output);

    while (true) {
        led_state = !led_state;
        hw::VirtualGpio::instance().write_pin(
            0, led_state ? hw::PinState::High : hw::PinState::Low
        );

        // Periodically dump task telemetry metrics to stdout
        TaskMetrics metrics[8];
        size_t count = Scheduler::instance().get_task_metrics(metrics, 8);

        std::cout << "\n--- [RTOS TELEMETRY @ Tick " << Scheduler::instance().tick_count() << "] ---" << std::endl;
        std::cout << "Name      | State   | Prio | StkFree | SwCnt" << std::endl;
        std::cout << "------------------------------------------" << std::endl;
        for (size_t i = 0; i < count; ++i) {
            const char* state_str = "READY";
            if (metrics[i].state == TaskState::Running) state_str = "RUN  ";
            else if (metrics[i].state == TaskState::Blocked) state_str = "BLOCK";
            else if (metrics[i].state == TaskState::Suspended) state_str = "SUSP ";

            std::printf("%-9s | %-7s | %-4u | %-7zu | %-5u\n",
                metrics[i].name,
                state_str,
                metrics[i].current_priority,
                metrics[i].stack_free_bytes,
                metrics[i].context_switch_count
            );
        }
        std::fflush(stdout);

        // Pull and display virtual UART output
        char uart_buf[256]{};
        size_t pulled = hw::VirtualUart::instance().pull_transmitted_data(uart_buf, sizeof(uart_buf) - 1);
        if (pulled > 0) {
            uart_buf[pulled] = '\0';
            std::cout << "[UART SINK] " << uart_buf;
        }

        Scheduler::instance().sleep_ticks(200); // 200 ms heartbeat
    }
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "       Starting myRTOS Simulation       " << std::endl;
    std::cout << "========================================" << std::endl;

    Status init_res = Scheduler::instance().init();
    if (init_res != Status::Ok) {
        std::cerr << "Failed to initialize scheduler: " << static_cast<int>(init_res) << std::endl;
        return 1;
    }

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

    std::cout << "Starting preemptive scheduler dispatcher..." << std::endl;
    Scheduler::instance().start();

    return 0;
}
