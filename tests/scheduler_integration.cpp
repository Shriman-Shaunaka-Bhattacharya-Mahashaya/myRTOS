#include "test_support.hpp"
#include "kernel/scheduler.hpp"
#include "kernel/semaphore.hpp"
#include "kernel/queue.hpp"
#include "port/port.hpp"
#include "hw/uart.hpp"
#include "hw/gpio.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <thread>

using namespace myrtos;

struct Scenario {
    BinarySemaphore signal{false};
    StaticQueue<unsigned, 4> messages;
    std::atomic<unsigned> received{0};
    std::atomic<unsigned> returned{0};
    std::atomic<unsigned> timeouts{0};
    alignas(16) uint8_t stacks[4][DEFAULT_STACK_SIZE_BYTES]{};
};

void producer(void* argument) {
    auto& scenario = *static_cast<Scenario*>(argument);
    for (unsigned value = 1;; ++value) {
        CHECK(scenario.messages.send(value) == Status::Ok);
        CHECK(scenario.signal.release() == Status::Ok);
        CHECK(Scheduler::instance().sleep_ticks(2) == Status::Ok);
    }
}
void consumer(void* argument) {
    auto& scenario = *static_cast<Scenario*>(argument);
    unsigned expected = 1;
    for (;;) {
        CHECK(scenario.signal.acquire() == Status::Ok);
        unsigned value = 0;
        CHECK(scenario.messages.receive(value) == Status::Ok);
        CHECK(value == expected++);
        scenario.received.fetch_add(1);
        hw::VirtualUart::instance().write_byte('!');
        uint8_t incoming = 0;
        hw::VirtualUart::instance().read_byte(&incoming);
        hw::VirtualGpio::instance().write_pin(0, value % 2 ? hw::PinState::High : hw::PinState::Low);
    }
}
void returning_task(void* argument) {
    static_cast<Scenario*>(argument)->returned.fetch_add(1);
}
void timed_waiter(void* argument) {
    auto& scenario = *static_cast<Scenario*>(argument);
    Semaphore never_signalled(0, 1);
    CHECK(never_signalled.acquire(3) == Status::Timeout);
    CHECK(never_signalled.waiting_count() == 0);
    CHECK(never_signalled.release() == Status::Ok);
    CHECK(never_signalled.try_acquire() == Status::Ok);
    scenario.timeouts.fetch_add(1);
}

int main() {
    // Recreate the UI/RTOS two-thread pattern and repeat after clean shutdown.
    for (int round = 0; round < 2; ++round) {
        Scenario scenario;
        std::promise<Status> initialized;
        auto ready = initialized.get_future();
        Status run_result = Status::Error;
        std::thread worker([&] {
            auto& scheduler = Scheduler::instance();
            CHECK(scheduler.init() == Status::Ok);
            hw::VirtualGpio::instance().configure_pin(0, hw::PinDirection::Output);
            CHECK(scheduler.create_task("producer", producer, &scenario, 3, scenario.stacks[0], sizeof(scenario.stacks[0])) == Status::Ok);
            CHECK(scheduler.create_task("consumer", consumer, &scenario, 2, scenario.stacks[1], sizeof(scenario.stacks[1])) == Status::Ok);
            CHECK(scheduler.create_task("returns", returning_task, &scenario, 1, scenario.stacks[2], sizeof(scenario.stacks[2])) == Status::Ok);
            CHECK(scheduler.create_task("timeout", timed_waiter, &scenario, 4, scenario.stacks[3], sizeof(scenario.stacks[3])) == Status::Ok);
            initialized.set_value(Status::Ok);
            run_result = scheduler.start();
        });
        CHECK(ready.get() == Status::Ok);
        TickType last_tick = 0;
        bool saw_terminated = false;
        for (int frame = 0; frame < 100; ++frame) {
            TaskMetrics metrics[MAX_TASKS + 1];
            TickType tick = 0;
            CHECK(Scheduler::instance().get_task_metrics(metrics, MAX_TASKS + 1, &tick) == 5);
            CHECK(tick >= last_tick);
            last_tick = tick;
            for (const auto& task : metrics) {
                if (task.state == TaskState::Terminated) saw_terminated = true;
            }
            char output[64];
            hw::VirtualUart::instance().pull_transmitted_data(output, sizeof(output));
            hw::VirtualUart::instance().simulate_incoming_data("x", 1);
            CHECK(hw::VirtualGpio::instance().get_pin_info(0).direction == hw::PinDirection::Output);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        Port::request_stop();
        worker.join();
        CHECK(run_result == Status::Ok);
        CHECK(!Scheduler::instance().is_running());
        CHECK(last_tick > 10);
        CHECK(scenario.received.load() >= 5);
        CHECK(scenario.returned.load() == 1);
        CHECK(scenario.timeouts.load() == 1);
        CHECK(saw_terminated);
    }
    std::puts("Scheduler integration: responsive snapshots, IPC, timeout, task exit, stop/restart passed.");
}
