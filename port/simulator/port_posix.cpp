#include "port/port.hpp"
#include "kernel/scheduler.hpp"
#include "hw/vic.hpp"

#if defined(__unix__) || defined(__APPLE__)

#include <ucontext.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <cstring>

namespace myrtos {

namespace {

ucontext_t g_main_context;
pthread_mutex_t g_critical_section = PTHREAD_MUTEX_INITIALIZER;
bool g_port_initialized{false};
pthread_t g_timer_thread;
volatile bool g_timer_running{false};

struct TaskContextShim {
    TaskControlBlock* tcb;
    TaskFunction func;
    void* arg;
};

TaskContextShim g_shims[MAX_TASKS];
size_t g_shim_count{0};

// Static storage for ucontext structures to avoid runtime heap allocation
ucontext_t g_contexts[MAX_TASKS];

void ContextTaskThunk(uint32_t shim_index) {
    TaskContextShim* shim = &g_shims[shim_index];
    TaskControlBlock* tcb = shim->tcb;
    TaskFunction entry = shim->func;
    void* arg = shim->arg;

    // Run the task entry function
    entry(arg);

    // If task returns, transition to Terminated
    tcb->state = TaskState::Terminated;
    Scheduler::instance().yield();

    while (true) {
        Port::yield();
    }
}

void* TimerWorkerThread(void* /*arg*/) {
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 1000000; // 1 millisecond (1,000,000 ns)

    while (g_timer_running) {
        nanosleep(&ts, nullptr);
        hw::VirtualInterruptController::instance().trigger_irq(hw::IRQ_SYSTICK);
    }
    return nullptr;
}

} // anonymous namespace

Status Port::init() noexcept {
    if (!g_port_initialized) {
        pthread_mutex_init(&g_critical_section, nullptr);
        getcontext(&g_main_context);

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

    uint32_t shim_index = static_cast<uint32_t>(g_shim_count++);
    TaskContextShim* shim = &g_shims[shim_index];
    shim->tcb = tcb;
    shim->func = entry_func;
    shim->arg = argument;

    ucontext_t* ctx = &g_contexts[shim_index];
    if (getcontext(ctx) == -1) {
        return Status::Error;
    }

    ctx->uc_stack.ss_sp = tcb->stack_base;
    ctx->uc_stack.ss_size = tcb->stack_size;
    ctx->uc_stack.ss_flags = 0;
    ctx->uc_link = &g_main_context;

    makecontext(ctx, reinterpret_cast<void (*)()>(ContextTaskThunk), 1, shim_index);

    tcb->port_context = ctx;
    return Status::Ok;
}

void Port::context_switch(
    TaskControlBlock* from_tcb,
    TaskControlBlock* to_tcb
) noexcept {
    if (to_tcb == nullptr || to_tcb->port_context == nullptr) {
        return;
    }

    // Process any pending virtual hardware interrupts before swapping
    hw::VirtualInterruptController::instance().dispatch_next_pending();

    auto* to_ctx = static_cast<ucontext_t*>(to_tcb->port_context);
    auto* from_ctx = (from_tcb && from_tcb->port_context) 
                     ? static_cast<ucontext_t*>(from_tcb->port_context) 
                     : &g_main_context;

    swapcontext(from_ctx, to_ctx);
}

[[noreturn]] void Port::start_first_task(TaskControlBlock* first_tcb) noexcept {
    // Launch simulated hardware SysTick timer thread
    g_timer_running = true;
    pthread_create(&g_timer_thread, nullptr, TimerWorkerThread, nullptr);

    auto* target_ctx = static_cast<ucontext_t*>(first_tcb->port_context);
    setcontext(target_ctx);

    // Unreachable
    while (true) {}
}

void Port::yield() noexcept {
    // Dispatch any pending interrupts
    hw::VirtualInterruptController::instance().dispatch_next_pending();

    TaskControlBlock* current = Scheduler::instance().current_task();
    if (current != nullptr && current->id == 0) {
        // Idle task: yield host CPU time slice to allow timer thread ticks to be processed
        usleep(1000); // 1 ms sleep
        hw::VirtualInterruptController::instance().dispatch_next_pending();
    }
}

uint32_t Port::enter_critical() noexcept {
    if (g_port_initialized) {
        pthread_mutex_lock(&g_critical_section);
    }
    return 0;
}

void Port::exit_critical(uint32_t /*prev_state*/) noexcept {
    if (g_port_initialized) {
        pthread_mutex_unlock(&g_critical_section);
    }
}

} // namespace myrtos

#endif // defined(__unix__) || defined(__APPLE__)
