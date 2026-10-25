#include "port/port.hpp"
#include "kernel/scheduler.hpp"
#include "hw/vic.hpp"
#include "host_runtime.hpp"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdlib>

namespace myrtos {
namespace {
void* main_fiber = nullptr;
struct Context {
    TaskControlBlock* task = nullptr;
    TaskFunction entry = nullptr;
    void* argument = nullptr;
};
Context contexts[MAX_TASKS + 1]; // The idle context is additional to application tasks.
size_t context_count = 0;

void release_contexts() noexcept {
    for (size_t i = 0; i < context_count; ++i) {
        auto* task = contexts[i].task;
        if (task->port_context) DeleteFiber(task->port_context);
        task->port_context = nullptr;
    }
    context_count = 0;
}

VOID WINAPI task_thunk(PVOID parameter) {
    auto* context = static_cast<Context*>(parameter);
    context->entry(context->argument);
    Scheduler::instance().finish_current_task();
    std::abort(); // A terminated task must never be selected again.
}
}

Status Port::init() noexcept {
    release_contexts();
    host::initialize();
    auto& vic = hw::VirtualInterruptController::instance();
    vic.register_handler(hw::IRQ_SYSTICK, [](void*) { Scheduler::instance().tick_isr(); }, nullptr, 0);
    vic.enable_irq(hw::IRQ_SYSTICK);
    return Status::Ok;
}

Status Port::create_context(TaskControlBlock* task, TaskFunction entry, void* argument) noexcept {
    if (!task || !entry) return Status::InvalidParam;
    if (context_count >= MAX_TASKS + 1) return Status::ResourceExhausted;
    auto* context = &contexts[context_count];
    *context = {task, entry, argument};
    task->port_context = CreateFiber(task->stack_size, task_thunk, context);
    if (!task->port_context) return Status::Error;
    ++context_count;
    return Status::Ok;
}

void Port::context_switch(TaskControlBlock* from, TaskControlBlock* target) noexcept {
    // All scheduler mutations for this transition precede the unlock. Each suspended
    // stack retains its own depth; another context always starts with no host lock.
    const auto tick = Scheduler::instance().tick_count();
    const auto depth = host::suspend_critical();
    host::transition(from ? from->id : 0, target->id, tick);
    if (host::stop_requested()) SwitchToFiber(main_fiber);
    SwitchToFiber(target->port_context);
    host::restore_critical(depth);
}

Status Port::start_first_task(TaskControlBlock* first) noexcept {
    if (!host::on_owner_thread() || !first || !first->port_context) return Status::InvalidParam;
    const bool already_fiber = IsThreadAFiber() != FALSE;
    main_fiber = already_fiber ? GetCurrentFiber() : ConvertThreadToFiber(nullptr);
    if (!main_fiber) return Status::Error;
    host::begin();
    host::transition(0, first->id, Scheduler::instance().tick_count());
    if (!host::stop_requested()) SwitchToFiber(first->port_context);
    // Stop abandons the suspended task stacks only after releasing their host locks.
    {
        CriticalSectionGuard guard;
        Scheduler::instance().cleanup_tasks();
        release_contexts();
    }
    if (!already_fiber) ConvertFiberToThread();
    main_fiber = nullptr;
    host::finish();
    return Status::Ok;
}

void Port::yield() noexcept {
    if (!host::on_owner_thread()) return;
    if (host::stop_requested()) {
        host::suspend_critical();
        SwitchToFiber(main_fiber);
        std::abort();
    }
    host::poll();
}
} // namespace myrtos
#endif
