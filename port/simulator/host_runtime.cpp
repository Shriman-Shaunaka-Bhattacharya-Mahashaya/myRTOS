#include "host_runtime.hpp"
#include "port/port.hpp"
#include "kernel/scheduler.hpp"
#include "hw/vic.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <mutex>
#include <thread>

namespace myrtos::host {
namespace {
std::recursive_mutex critical_mutex;
thread_local uint32_t critical_depth = 0;
thread_local bool owner_thread = false;
std::atomic<bool> stop{false};
// Only the scheduler thread advances this clock. No timer thread edits the VIC.
std::chrono::steady_clock::time_point next_tick;
}

uint32_t enter_critical() noexcept {
    critical_mutex.lock();
    return critical_depth++;
}

void exit_critical(uint32_t previous_depth) noexcept {
    assert(critical_depth == previous_depth + 1);
    (void)previous_depth;
    --critical_depth;
    critical_mutex.unlock();
}

uint32_t suspend_critical() noexcept {
    const auto depth = critical_depth;
    while (critical_depth != 0) {
        --critical_depth;
        critical_mutex.unlock();
    }
    return depth;
}

void restore_critical(uint32_t depth) noexcept {
    assert(critical_depth == 0);
    while (critical_depth < depth) {
        critical_mutex.lock();
        ++critical_depth;
    }
}

void initialize() noexcept {
    owner_thread = true;
    stop.store(false);
}
void begin() noexcept {
    next_tick = std::chrono::steady_clock::now() + std::chrono::milliseconds(1);
}
void finish() noexcept { owner_thread = false; }
void request_stop() noexcept { stop.store(true); }
bool stop_requested() noexcept { return stop.load(); }
bool on_owner_thread() noexcept { return owner_thread; }

void poll() noexcept {
    if (!owner_thread) return;
    hw::VirtualInterruptController::instance().dispatch_next_pending();
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_tick) {
        next_tick += std::chrono::milliseconds(1);
        Scheduler::instance().tick_isr();
    } else {
        auto* current = Scheduler::instance().current_task();
        if (current != nullptr && current->id == 0) {
            std::this_thread::sleep_until(next_tick);
        }
    }
}
} // namespace myrtos::host

namespace myrtos {
uint32_t Port::enter_critical() noexcept { return host::enter_critical(); }
void Port::exit_critical(uint32_t state) noexcept { host::exit_critical(state); }
void Port::request_stop() noexcept { host::request_stop(); }
bool Port::on_owner_thread() noexcept { return host::on_owner_thread(); }
}
