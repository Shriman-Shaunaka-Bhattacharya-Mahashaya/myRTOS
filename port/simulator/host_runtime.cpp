#include "host_runtime.hpp"
#include "port/port.hpp"
#include "kernel/scheduler.hpp"
#include "hw/vic.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <cmath>
#include "port/simulation.hpp"

namespace myrtos::host {
namespace {
std::recursive_mutex critical_mutex;
thread_local uint32_t critical_depth = 0;
thread_local bool owner_thread = false;
std::atomic<bool> stop{false};
// Only the scheduler thread advances this clock. No timer thread edits the VIC.
std::chrono::steady_clock::time_point next_tick;
std::mutex control_mutex;
std::condition_variable control_changed;
SimulationState control{false, false, false, 1, 0};
bool rebase = true;
bool transition_permit = false;
uint64_t transition_revision = 0;
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
    std::lock_guard lock(control_mutex);
    control = {false, false, false, 1, 0};
    transition_permit = false;
    transition_revision = 0;
    rebase = true;
}
void begin() noexcept {
    std::lock_guard lock(control_mutex);
    control.running = true;
    rebase = true;
}
void finish() noexcept {
    owner_thread = false;
    std::lock_guard lock(control_mutex);
    control.running = false;
    control.transition_waiting = false;
    transition_permit = false;
    control.pause_requested = false;
    control.paused = false;
    control.pending_steps = 0;
    control_changed.notify_all();
}
void request_stop() noexcept {
    std::lock_guard lock(control_mutex);
    stop.store(true); control_changed.notify_all();
}
bool stop_requested() noexcept { return stop.load(); }
bool on_owner_thread() noexcept { return owner_thread; }

void transition(uint32_t from, uint32_t to, uint32_t tick) noexcept {
    // Called only after the scheduler committed the dispatch and the port
    // released all host-lock depth. The UI may inspect a coherent live state.
    assert(critical_depth == 0);
    std::unique_lock lock(control_mutex);
    if (control.transition_mode == TransitionMode::Off || stop.load()) return;
    control.transition_from = from; control.transition_to = to; control.transition_tick = tick;
    ++control.transition_sequence;
    control.transition_waiting = true;
    transition_permit = false;
    auto revision = transition_revision;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(control.transition_delay_ms);
    while (!stop.load() && control.transition_mode != TransitionMode::Off) {
        if (revision != transition_revision) {
            revision = transition_revision;
            deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(control.transition_delay_ms);
        }
        if (transition_permit) break;
        if (control.transition_mode == TransitionMode::Manual) control_changed.wait(lock);
        else if (std::chrono::steady_clock::now() >= deadline) break;
        else control_changed.wait_until(lock, deadline);
    }
    transition_permit = false;
    control.transition_waiting = false;
    // Teaching delays do not count as elapsed simulation time.
    rebase = true;
}

void poll() noexcept {
    if (!owner_thread) return;
    // This is called by idle after application tasks have blocked. Never hold
    // the kernel mutex while waiting for a UI command.
    const auto depth = suspend_critical();
    bool advance = false;
    {
        std::unique_lock lock(control_mutex);
        while (!stop.load()) {
            if (control.pause_requested) {
                control.paused = control.pending_steps == 0;
                rebase = true;
                if (control.pending_steps) {
                    --control.pending_steps;
                    advance = true;
                    break;
                }
                control_changed.wait(lock);
                continue;
            }
            control.paused = false;
            const auto interval = std::chrono::nanoseconds(static_cast<int64_t>(1000000.0 / control.speed));
            if (rebase) { next_tick = std::chrono::steady_clock::now() + interval; rebase = false; }
            if (std::chrono::steady_clock::now() >= next_tick) {
                next_tick += interval;
                advance = true;
                break;
            }
            control_changed.wait_until(lock, next_tick);
        }
    }
    restore_critical(depth);
    if (advance && !stop.load()) {
        // Both interrupt handlers and tick callbacks execute on the owner thread.
        hw::VirtualInterruptController::instance().dispatch_next_pending();
        Scheduler::instance().tick_isr();
    }
}
} // namespace myrtos::host

namespace myrtos {
SimulationState SimulationControl::state() noexcept {
    std::lock_guard lock(host::control_mutex); return host::control;
}
void SimulationControl::pause(bool paused) noexcept {
    std::lock_guard lock(host::control_mutex);
    if (host::control.transition_mode != TransitionMode::Off) return;
    host::control.pause_requested = paused;
    host::control.paused = false;
    if (!paused) host::control.pending_steps = 0;
    host::rebase = true;
    host::control_changed.notify_all();
}
bool SimulationControl::step() noexcept {
    std::lock_guard lock(host::control_mutex);
    if (!host::control.running || !host::control.pause_requested || host::control.pending_steps >= 1000) return false;
    ++host::control.pending_steps;
    host::control.paused = false;
    host::control_changed.notify_all();
    return true;
}
bool SimulationControl::speed(double multiplier) noexcept {
    if (!std::isfinite(multiplier) || multiplier < 0.0005 || multiplier > 10) return false;
    std::lock_guard lock(host::control_mutex);
    host::control.speed = multiplier;
    host::rebase = true;
    host::control_changed.notify_all();
    return true;
}
bool SimulationControl::transitions(TransitionMode mode, unsigned delay_ms) noexcept {
    if ((mode != TransitionMode::Off && mode != TransitionMode::Manual && mode != TransitionMode::Automatic) ||
        delay_ms < 100 || delay_ms > 10000) return false;
    std::lock_guard lock(host::control_mutex);
    host::control.transition_mode = mode;
    host::control.transition_delay_ms = delay_ms;
    host::control.pause_requested = false;
    host::control.paused = false;
    host::control.pending_steps = 0;
    host::transition_permit = false;
    ++host::transition_revision;
    host::rebase = true;
    host::control_changed.notify_all();
    return true;
}
bool SimulationControl::next_transition() noexcept {
    std::lock_guard lock(host::control_mutex);
    if (!host::control.running || !host::control.transition_waiting ||
        host::control.transition_mode != TransitionMode::Manual || host::transition_permit) return false;
    host::transition_permit = true;
    host::control_changed.notify_all();
    return true;
}
uint32_t Port::enter_critical() noexcept { return host::enter_critical(); }
void Port::exit_critical(uint32_t state) noexcept { host::exit_critical(state); }
void Port::request_stop() noexcept { host::request_stop(); }
bool Port::on_owner_thread() noexcept { return host::on_owner_thread(); }
}
