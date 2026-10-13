#include "port/port.hpp"
#include "kernel/scheduler.hpp"
#include "hw/vic.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <process.h>
#include <mmsystem.h>

namespace myrtos {

namespace {

void* g_main_fiber{nullptr};
CRITICAL_SECTION g_critical_section;
bool g_port_initialized{false};
HANDLE g_timer_thread{nullptr};
volatile bool g_timer_running{false};

struct TaskContextShim {
    TaskControlBlock* tcb;
    TaskFunction func;
    void* arg;
};

// Fixed pool of shim structs to avoid heap allocations
TaskContextShim g_shims[MAX_TASKS];
size_t g_shim_count{0};

VOID WINAPI FiberTaskThunk(PVOID lpParameter) {
    auto* shim = static_cast<TaskContextShim*>(lpParameter);
    TaskControlBlock* tcb = shim->tcb;
    TaskFunction entry = shim->func;
    void* arg = shim->arg;

    // Run the task entry function
    entry(arg);

    // If task returns, transition to Terminated
    tcb->state = TaskState::Terminated;
    Scheduler::instance().yield();

    // Catch-all loop if task is somehow resumed
    while (true) {
        Port::yield();
    }
}

unsigned __stdcall TimerWorkerThread(void* /*arg*/) {
    while (g_timer_running) {
        Sleep(1); // 1 millisecond host resolution
        hw::VirtualInterruptController::instance().trigger_irq(hw::IRQ_SYSTICK);
    }
    return 0;
}

} // anonymous namespace

Status Port::init() noexcept {
    if (!g_port_initialized) {
        InitializeCriticalSection(&g_critical_section);
        timeBeginPeriod(1);

        // Convert current host thread into primary fiber
        g_main_fiber = ConvertThreadToFiber(nullptr);
        if (g_main_fiber == nullptr) {
            // Already converted or error
            g_main_fiber = GetCurrentFiber();
        }

        // Register default SysTick handler in Virtual Interrupt Controller
        hw::VirtualInterruptController::instance().register_handler(
            hw::IRQ_SYSTICK,
            [](void* /*ctx*/) {
                Scheduler::instance().tick_isr();
            },
            nullptr,
            0 // Highest IRQ priority
        );
        hw::VirtualInterruptController::instance().enable_irq(hw::IRQ_SYSTICK);

        g_port_initialized = true;
    }
    return Status::Ok;
}

Status Port::create_context(
    TaskControlBlock* tcb,
    TaskFunction entry_func,
    void* argument
) noexcept {
    if (tcb == nullptr || entry_func == nullptr) {
        return Status::InvalidParam;
    }

    if (g_shim_count >= MAX_TASKS) {
        return Status::ResourceExhausted;
    }

    TaskContextShim* shim = &g_shims[g_shim_count++];
    shim->tcb = tcb;
    shim->func = entry_func;
    shim->arg = argument;

    // Create independent Win32 Fiber with isolated stack
    void* fiber_handle = CreateFiber(
        tcb->stack_size,
        FiberTaskThunk,
        shim
    );

    if (fiber_handle == nullptr) {
        return Status::Error;
    }

    tcb->port_context = fiber_handle;
    return Status::Ok;
}

void Port::context_switch(
    TaskControlBlock* /*from_tcb*/,
    TaskControlBlock* to_tcb
) noexcept {
    if (to_tcb == nullptr || to_tcb->port_context == nullptr) {
        return;
    }

    // Process any pending virtual hardware interrupts before swapping
    hw::VirtualInterruptController::instance().dispatch_next_pending();

    void* target_fiber = to_tcb->port_context;
    SwitchToFiber(target_fiber);
}

[[noreturn]] void Port::start_first_task(TaskControlBlock* first_tcb) noexcept {
    // Ensure the calling thread is converted to a fiber so SwitchToFiber succeeds
    void* current_fiber = ConvertThreadToFiber(nullptr);
    if (current_fiber == nullptr && GetLastError() == ERROR_ALREADY_FIBER) {
        current_fiber = GetCurrentFiber();
    }
    g_main_fiber = current_fiber;

    // Launch simulated hardware SysTick timer thread
    g_timer_running = true;
    g_timer_thread = reinterpret_cast<HANDLE>(_beginthreadex(
        nullptr, 0, TimerWorkerThread, nullptr, 0, nullptr
    ));

    void* target_fiber = first_tcb->port_context;
    SwitchToFiber(target_fiber);

    // Unreachable
    while (true) {}
}

void Port::yield() noexcept {
    // Dispatch any pending interrupts
    hw::VirtualInterruptController::instance().dispatch_next_pending();

    TaskControlBlock* current = Scheduler::instance().current_task();
    if (current != nullptr && current->id == 0) {
        // Idle task: yield host thread slice to allow timer thread ticks to be processed
        Sleep(1);
        hw::VirtualInterruptController::instance().dispatch_next_pending();
    }
}

uint32_t Port::enter_critical() noexcept {
    if (g_port_initialized) {
        EnterCriticalSection(&g_critical_section);
    }
    return 0;
}

void Port::exit_critical(uint32_t /*prev_state*/) noexcept {
    if (g_port_initialized) {
        LeaveCriticalSection(&g_critical_section);
    }
}

} // namespace myrtos

#endif // _WIN32
