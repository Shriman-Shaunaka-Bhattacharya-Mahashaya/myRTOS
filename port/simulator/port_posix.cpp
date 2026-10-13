#include "port/port.hpp"
#include "kernel/scheduler.hpp"
#include "hw/vic.hpp"
#include "host_runtime.hpp"
#if defined(__unix__) || defined(__APPLE__)
#include <ucontext.h>
#include <cstdlib>

namespace myrtos {
namespace {
ucontext_t main_context;
struct Context {
    TaskControlBlock* task = nullptr;
    TaskFunction entry = nullptr;
    void* argument = nullptr;
    ucontext_t machine{};
};
Context contexts[MAX_TASKS + 1];
size_t context_count = 0;

void task_thunk(unsigned index) {
    auto& context = contexts[index];
    context.entry(context.argument);
    Scheduler::instance().finish_current_task();
    std::abort();
}
}

Status Port::init() noexcept {
    context_count = 0;
    host::initialize();
    auto& vic = hw::VirtualInterruptController::instance();
    vic.register_handler(hw::IRQ_SYSTICK, [](void*) { Scheduler::instance().tick_isr(); }, nullptr, 0);
    vic.enable_irq(hw::IRQ_SYSTICK);
    return Status::Ok;
}

Status Port::create_context(TaskControlBlock* task, TaskFunction entry, void* argument) noexcept {
    if (!task || !entry) return Status::InvalidParam;
    if (context_count >= MAX_TASKS + 1) return Status::ResourceExhausted;
    auto& context = contexts[context_count];
    context.task = task;
    context.entry = entry;
    context.argument = argument;
    if (getcontext(&context.machine) == -1) return Status::Error;
    // Preserve the bottom canary rather than offering it as usable stack storage.
    context.machine.uc_stack.ss_sp = task->stack_base + 16;
    context.machine.uc_stack.ss_size = task->stack_size - 16;
    context.machine.uc_stack.ss_flags = 0;
    context.machine.uc_link = &main_context;
    makecontext(&context.machine, reinterpret_cast<void (*)()>(task_thunk), 1,
                static_cast<unsigned>(context_count));
    task->port_context = &context.machine;
    ++context_count;
    return Status::Ok;
}

void Port::context_switch(TaskControlBlock* from, TaskControlBlock* target) noexcept {
    const auto depth = host::suspend_critical();
    if (host::stop_requested()) {
        setcontext(&main_context);
        std::abort();
    }
    if (swapcontext(static_cast<ucontext_t*>(from->port_context),
                    static_cast<ucontext_t*>(target->port_context)) == -1) std::abort();
    host::restore_critical(depth);
}

Status Port::start_first_task(TaskControlBlock* first) noexcept {
    if (!host::on_owner_thread() || !first || !first->port_context) return Status::InvalidParam;
    host::begin();
    const int result = swapcontext(&main_context, static_cast<ucontext_t*>(first->port_context));
    {
        CriticalSectionGuard guard;
        for (size_t i = 0; i < context_count; ++i) contexts[i].task->port_context = nullptr;
        context_count = 0;
    }
    host::finish();
    return result == 0 ? Status::Ok : Status::Error;
}

void Port::yield() noexcept {
    if (!host::on_owner_thread()) return;
    if (host::stop_requested()) {
        host::suspend_critical();
        setcontext(&main_context);
        std::abort();
    }
    host::poll();
}
} // namespace myrtos
#endif
