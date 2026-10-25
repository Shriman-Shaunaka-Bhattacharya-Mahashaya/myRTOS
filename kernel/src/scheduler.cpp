#include "kernel/scheduler.hpp"
#include "kernel/timer.hpp"
#include "kernel/mutex.hpp"
#include "port/port.hpp"
#include <cstring>
#include <new>

namespace myrtos {

Scheduler& Scheduler::instance() noexcept {
    static Scheduler s_instance;
    return s_instance;
}

Scheduler::Scheduler() noexcept {
    ready_bitmap_ = 0;
    current_tick_ = 0;
    lock_nesting_ = 0;
    is_running_ = false;
    reschedule_pending_ = false;
    current_task_ = nullptr;
    next_task_ = nullptr;
    task_count_ = 0;
    next_task_id_ = 1;

    for (size_t i = 0; i < MAX_TASKS; ++i) {
        task_registry_[i] = nullptr;
    }
}

Status Scheduler::init() noexcept {
    CriticalSectionGuard guard;
    if (is_running_) return Status::Error;
    current_task_ = nullptr;
    next_task_ = nullptr;

    TimerManager::instance().reset();
    trace_sequence_ = 0;
    ready_bitmap_ = 0;
    current_tick_ = 0;
    lock_nesting_ = 0;
    is_running_ = false;
    reschedule_pending_ = false;
    task_count_ = 0;
    next_task_id_ = 1;

    for (size_t i = 0; i < MAX_PRIORITIES; ++i) {
        ready_queues_[i].reset();
    }
    delayed_list_.reset();
    suspended_list_.reset();

    for (size_t i = 0; i < MAX_TASKS; ++i) {
        task_registry_[i] = nullptr;
    }

    // Initialize Port layer (e.g. Win32 fibers)
    Status port_status = Port::init();
    if (port_status != Status::Ok) {
        return port_status;
    }

    // Create Idle Task at LOWEST_PRIORITY (31)
    new (&idle_tcb_) TaskControlBlock();
    idle_tcb_.id = 0;
    std::strncpy(idle_tcb_.name, "IDLE", TASK_NAME_MAX_LEN - 1);
    idle_tcb_.base_priority = IDLE_TASK_PRIORITY;
    idle_tcb_.current_priority = IDLE_TASK_PRIORITY;
    idle_tcb_.state = TaskState::Ready;
    idle_tcb_.prepare_stack(idle_stack_, sizeof(idle_stack_));

    Status ctx_status = Port::create_context(&idle_tcb_, idle_task_func, nullptr);
    if (ctx_status != Status::Ok) {
        return ctx_status;
    }

    make_ready(&idle_tcb_);
    return Status::Ok;
}

Status Scheduler::create_task(
    const char* name,
    TaskFunction function,
    void* argument,
    Priority priority,
    uint8_t* stack_memory,
    size_t stack_size,
    TaskId* out_task_id
) noexcept {
    if (function == nullptr || stack_memory == nullptr || stack_size < MIN_STACK_SIZE_BYTES ||
        reinterpret_cast<uintptr_t>(stack_memory) % 16 != 0) {
        return Status::InvalidParam;
    }
    if (priority >= IDLE_TASK_PRIORITY) {
        return Status::InvalidParam;
    }

    CriticalSectionGuard guard;

    if (!Port::on_owner_thread()) return Status::NotInitialized;
    if (task_count_ >= MAX_TASKS) {
        return Status::ResourceExhausted;
    }

    // Statically bind TCB inside top or caller allocated space
    // For simplicity, we allocate TCB struct at the bottom of the provided stack memory!
    // Stack layout: [Canary][TCB struct][Usable Task Stack space...]
    constexpr size_t tcb_aligned_size = (sizeof(TaskControlBlock) + 15) & ~size_t(15);
    if (stack_size <= tcb_aligned_size + MIN_STACK_SIZE_BYTES) {
        return Status::InvalidParam;
    }

    auto* tcb = reinterpret_cast<TaskControlBlock*>(stack_memory);
    new (tcb) TaskControlBlock();

    uint8_t* task_stack_buf = stack_memory + tcb_aligned_size;
    size_t task_stack_size = stack_size - tcb_aligned_size;

    tcb->id = next_task_id_++;
    std::strncpy(tcb->name, name ? name : "task", TASK_NAME_MAX_LEN - 1);
    tcb->name[TASK_NAME_MAX_LEN - 1] = '\0';
    tcb->base_priority = priority;
    tcb->current_priority = priority;
    tcb->state = TaskState::Ready;
    tcb->state_node = ListNode(tcb, priority);
    tcb->event_node = ListNode(tcb, 0);

    tcb->prepare_stack(task_stack_buf, task_stack_size);

    Status ctx_res = Port::create_context(tcb, function, argument);
    if (ctx_res != Status::Ok) {
        return ctx_res;
    }

    task_registry_[task_count_++] = tcb;
    make_ready(tcb);

    if (out_task_id != nullptr) {
        *out_task_id = tcb->id;
    }

    if (is_running_) {
        schedule();
    }

    return Status::Ok;
}

void Scheduler::make_ready(TaskControlBlock* task) noexcept {
    if (task == nullptr) return;

    task->state = TaskState::Ready;
    Priority prio = task->current_priority;
    if (prio >= MAX_PRIORITIES) prio = IDLE_TASK_PRIORITY;

    ready_queues_[prio].push_back(&task->state_node);
    ready_bitmap_ |= (1U << (31U - prio));
}

void Scheduler::remove_from_ready(TaskControlBlock* task) noexcept {
    if (task == nullptr) return;

    Priority prio = task->current_priority;
    if (prio >= MAX_PRIORITIES) prio = IDLE_TASK_PRIORITY;

    ready_queues_[prio].remove(&task->state_node);
    if (ready_queues_[prio].empty()) {
        ready_bitmap_ &= ~(1U << (31U - prio));
    }
}

Status Scheduler::start() noexcept {

    TaskControlBlock* first_task = nullptr;
    {
        CriticalSectionGuard guard;

        if (is_running_ || !Port::on_owner_thread() || ready_bitmap_ == 0) return Status::InvalidParam;
        // Resolve first task to run
        uint32_t top_prio = count_leading_zeros(ready_bitmap_);
        if (top_prio >= MAX_PRIORITIES) {
            top_prio = IDLE_TASK_PRIORITY;
        }

        current_task_ = static_cast<TaskControlBlock*>(ready_queues_[top_prio].head()->owner);
        current_task_->state = TaskState::Running;
        current_task_->context_switch_count = 1;
        current_task_->last_run_tick = current_tick_;
        is_running_ = true;
        first_task = current_task_;
        record_switch(0, first_task->id);
    }

    const auto result = Port::start_first_task(first_task);
    {
        CriticalSectionGuard guard;
        is_running_ = false;
        current_task_ = nullptr;
        next_task_ = nullptr;
    }
    return result;
}

void Scheduler::yield() noexcept {
    CriticalSectionGuard guard;
    if (!is_running_ || !Port::on_owner_thread()) return;

    if (current_task_->state == TaskState::Running) {
        // Move to the back of equal priority ready queue for round-robin
        remove_from_ready(current_task_);
        make_ready(current_task_);
    }

    schedule();
}

void Scheduler::schedule() noexcept {
    if (!is_running_) return;
    if (!Port::on_owner_thread()) { reschedule_pending_ = true; return; }
    if (lock_nesting_ > 0) {
        reschedule_pending_ = true;
        return;
    }

    uint32_t top_prio = count_leading_zeros(ready_bitmap_);
    if (top_prio >= MAX_PRIORITIES) {
        top_prio = IDLE_TASK_PRIORITY;
    }

    TaskControlBlock* best_task = static_cast<TaskControlBlock*>(ready_queues_[top_prio].head()->owner);
    if (best_task == current_task_) {
        best_task->state = TaskState::Running;
        return;
    }

    TaskControlBlock* prev_task = current_task_;
    if (prev_task != nullptr && prev_task->state == TaskState::Running) {
        prev_task->state = TaskState::Ready;
    }

    next_task_ = best_task;
    current_task_ = next_task_;
    current_task_->state = TaskState::Running;
    current_task_->context_switch_count++;
    current_task_->last_run_tick = current_tick_;

    record_switch(prev_task ? prev_task->id : 0, current_task_->id);
    Port::context_switch(prev_task, current_task_);
}

void Scheduler::tick_isr() noexcept {
    CriticalSectionGuard guard;
    if (is_running_ && !Port::on_owner_thread()) return;
    ++current_tick_;

    if (current_task_ != nullptr) {
        ++current_task_->runtime_ticks;
    }

    lock(); // Commit tick, timer and wakeup changes before switching tasks.
    const auto delayed_count = delayed_list_.size();
    for (size_t i = 0; i < delayed_count; ++i) {
        auto* node = delayed_list_.pop_front();
        auto* task = static_cast<TaskControlBlock*>(node->owner);
        if (tick_reached(current_tick_, task->wake_tick)) {
            task->wait_result = task->wait_queue ? Status::Timeout : Status::Ok;
            if (task->wait_queue) task->wait_queue->remove(&task->event_node);
            task->wait_queue = nullptr;
            task->waiting_mutex = nullptr;
            make_ready(task);
        } else delayed_list_.push_back(node);
    }
    Mutex::recompute_priorities();

    // 2. Process Software Timers
    TimerManager::instance().process_timers(current_tick_);

    // 3. Preemptive Round-Robin for tasks at the same priority level
    if (is_running_ && current_task_ != nullptr && current_task_->state == TaskState::Running) {
        Priority prio = current_task_->current_priority;
        if (ready_queues_[prio].size() > 1) {
            remove_from_ready(current_task_);
            make_ready(current_task_);
        }
    }

    // Apply all mutations before dispatch.
    reschedule_pending_ = true;
    unlock();
}

Status Scheduler::sleep_ticks(TickType ticks) noexcept {
    CriticalSectionGuard guard;
    if (!can_block() || ticks > MAX_FINITE_WAIT) {
        return Status::InvalidParam; // Idle task cannot sleep
    }

    if (ticks == 0) {
        yield();
        return Status::Ok;
    }

    current_task_->wait_result = Status::Ok;
    current_task_->wake_tick = current_tick_ + ticks;
    current_task_->state_node.value = current_task_->wake_tick;
    current_task_->state = TaskState::Blocked;

    remove_from_ready(current_task_);
    delayed_list_.push_back(&current_task_->state_node);

    schedule();
    return current_task_->wait_result;
}

Status Scheduler::suspend_task(TaskId id) noexcept {
    if (!Port::on_owner_thread()) return Status::InvalidParam;
    CriticalSectionGuard guard;

    TaskControlBlock* target = nullptr;
    if (id == INVALID_TASK_ID || (current_task_ != nullptr && current_task_->id == id)) {
        target = current_task_;
    } else {
        for (size_t i = 0; i < task_count_; ++i) {
            if (task_registry_[i] != nullptr && task_registry_[i]->id == id) {
                target = task_registry_[i];
                break;
            }
        }
    }

    if (target == nullptr || target->id == 0) {
        return Status::InvalidParam; // Cannot suspend idle task
    }

    if (target->state == TaskState::Suspended || target->state == TaskState::Terminated) return Status::InvalidParam;
    if (target->wait_queue) {
        target->wait_queue->remove(&target->event_node);
        target->wait_queue = nullptr;
        target->wait_result = Status::Error;
    }
    if (target->state == TaskState::Ready || target->state == TaskState::Running) {
        remove_from_ready(target);
    } else if (target->state == TaskState::Blocked) {
        delayed_list_.remove(&target->state_node);
    }

    target->waiting_mutex = nullptr;
    target->state = TaskState::Suspended;
    suspended_list_.push_back(&target->state_node);
    Mutex::recompute_priorities();

    if (target == current_task_) {
        schedule();
    }
    return Status::Ok;
}

Status Scheduler::resume_task(TaskId id) noexcept {
    if (!Port::on_owner_thread()) return Status::InvalidParam;
    CriticalSectionGuard guard;

    TaskControlBlock* target = nullptr;
    for (size_t i = 0; i < task_count_; ++i) {
        if (task_registry_[i] != nullptr && task_registry_[i]->id == id) {
            target = task_registry_[i];
            break;
        }
    }

    if (target == nullptr || target->state != TaskState::Suspended) {
        return Status::NotFound;
    }

    suspended_list_.remove(&target->state_node);
    make_ready(target);

    if (is_running_) {
        schedule();
    }
    return Status::Ok;
}

void Scheduler::change_task_priority(TaskControlBlock* task, Priority new_priority) noexcept {
    CriticalSectionGuard guard;
    if (task == nullptr || task->id == 0 || new_priority >= MAX_PRIORITIES) return;

    if (task->current_priority == new_priority) return;

    if (task->state == TaskState::Ready || task->state == TaskState::Running) {
        remove_from_ready(task);
        task->current_priority = new_priority;
        make_ready(task);
    } else {
        task->current_priority = new_priority;
        if (task->wait_queue) {
            task->wait_queue->remove(&task->event_node);
            task->event_node.value = new_priority;
            task->wait_queue->insert_sorted_ascending(&task->event_node);
        }
    }

    if (is_running_) {
        schedule();
    }
}

Status Scheduler::block_current_task(IntrusiveList& wait_queue, TickType timeout_ticks) noexcept {
    CriticalSectionGuard guard;
    if (!can_block() || !valid_timeout(timeout_ticks) || timeout_ticks == 0) return Status::InvalidParam;

    remove_from_ready(current_task_);
    current_task_->state = TaskState::Blocked;
    current_task_->wait_result = Status::Ok;

    // Insert into event wait queue sorted by priority (highest priority = lowest number first)
    current_task_->event_node.value = current_task_->current_priority;
    current_task_->wait_queue = &wait_queue;
    wait_queue.insert_sorted_ascending(&current_task_->event_node);

    // If timeout specified, also link into delayed list
    if (timeout_ticks != WAIT_FOREVER && timeout_ticks > 0) {
        current_task_->wake_tick = current_tick_ + timeout_ticks;
        current_task_->state_node.value = current_task_->wake_tick;
        delayed_list_.push_back(&current_task_->state_node);
    }

    Mutex::recompute_priorities();
    schedule();
    return current_task_->wait_result;
}

void Scheduler::unblock_task(TaskControlBlock* task, Status result) noexcept {
    CriticalSectionGuard guard;
    if (task == nullptr || task->state != TaskState::Blocked) return;
    if (task->wait_queue) {
        task->wait_queue->remove(&task->event_node);
        task->wait_queue = nullptr;
    }
    task->waiting_mutex = nullptr;
    task->wait_result = result;

    if (task->state_node.is_linked()) {
        delayed_list_.remove(&task->state_node);
    }

    make_ready(task);

    if (is_running_) {
        schedule();
    }
}

void Scheduler::lock() noexcept {
    CriticalSectionGuard guard;
    ++lock_nesting_;
}

void Scheduler::unlock() noexcept {
    CriticalSectionGuard guard;
    if (lock_nesting_ > 0) {
        --lock_nesting_;
        if (lock_nesting_ == 0 && reschedule_pending_) {
            reschedule_pending_ = false;
            schedule();
        }
    }
}

size_t Scheduler::get_task_metrics(TaskMetrics* out_buffer, size_t max_count, TickType* sampled_tick) const noexcept {
    CriticalSectionGuard guard;
    if (sampled_tick) *sampled_tick = current_tick_;
    if (out_buffer == nullptr || max_count == 0) return 0;

    size_t count = 0;
    // Include Idle task
    if (count < max_count) {
        out_buffer[count].id = idle_tcb_.id;
        std::strncpy(out_buffer[count].name, idle_tcb_.name, TASK_NAME_MAX_LEN);
        out_buffer[count].state = idle_tcb_.state;
        out_buffer[count].base_priority = idle_tcb_.base_priority;
        out_buffer[count].current_priority = idle_tcb_.current_priority;
        out_buffer[count].stack_metrics_available = false;
        out_buffer[count].stack_size = idle_tcb_.stack_size;
        out_buffer[count].stack_free_bytes = 0;
        out_buffer[count].stack_usage_percent = 0.0f;
        out_buffer[count].runtime_ticks = idle_tcb_.runtime_ticks;
        out_buffer[count].context_switch_count = idle_tcb_.context_switch_count;
        out_buffer[count].last_run_tick = idle_tcb_.last_run_tick;
        ++count;
    }

    for (size_t i = 0; i < task_count_ && count < max_count; ++i) {
        if (task_registry_[i] != nullptr) {
            const auto* t = task_registry_[i];
            out_buffer[count].id = t->id;
            std::strncpy(out_buffer[count].name, t->name, TASK_NAME_MAX_LEN);
            out_buffer[count].state = t->state;
            out_buffer[count].base_priority = t->base_priority;
            out_buffer[count].current_priority = t->current_priority;
            out_buffer[count].stack_metrics_available = false;
            out_buffer[count].stack_size = t->stack_size;
            out_buffer[count].stack_free_bytes = 0;
            out_buffer[count].stack_usage_percent = 0.0f;
            out_buffer[count].runtime_ticks = t->runtime_ticks;
            out_buffer[count].context_switch_count = t->context_switch_count;
            out_buffer[count].last_run_tick = t->last_run_tick;
            ++count;
        }
    }
    return count;
}

bool Scheduler::is_running() const noexcept {
    CriticalSectionGuard guard;
    return is_running_;
}
TickType Scheduler::tick_count() const noexcept {
    CriticalSectionGuard guard;
    return current_tick_;
}
size_t Scheduler::task_count() const noexcept {
    CriticalSectionGuard guard;
    return task_count_;
}
void Scheduler::finish_current_task() noexcept {
    CriticalSectionGuard guard;
    if (!current_task_ || current_task_->id == 0) return;
    lock();
    Mutex::release_all(current_task_);
    remove_from_ready(current_task_);
    current_task_->state = TaskState::Terminated;
    reschedule_pending_ = true;
    unlock();
    schedule();
}

void Scheduler::cleanup_tasks() noexcept {
    CriticalSectionGuard guard;
    TimerManager::instance().reset();
    for (size_t i=0;i<task_count_;++i) {
        auto* task=task_registry_[i];
        if(task->event_node.list) task->event_node.list->remove(&task->event_node);
        if(task->state_node.list) task->state_node.list->remove(&task->state_node);
        task->wait_queue=nullptr; task->waiting_mutex=nullptr;
        Mutex::abandon_all(task);
        task->state=TaskState::Terminated;
    }
    for(auto& queue:ready_queues_) queue.reset();
    delayed_list_.reset(); suspended_list_.reset(); ready_bitmap_=0;
    idle_tcb_.state=TaskState::Terminated;
}
bool Scheduler::can_block() const noexcept {
    return Port::on_owner_thread() && is_running_ && current_task_ && current_task_->id != 0 && lock_nesting_ == 0;
}
Status Scheduler::set_initial_tick(TickType tick) noexcept {
    CriticalSectionGuard guard;
    if (is_running_) return Status::Error;
    current_tick_ = tick; return Status::Ok;
}
void Scheduler::record_switch(TaskId from, TaskId to) noexcept {
    auto& event = trace_[trace_sequence_ % TRACE_CAPACITY];
    event = {trace_sequence_ + 1, current_tick_, from, to};
    ++trace_sequence_;
}
size_t Scheduler::get_trace(SwitchEvent* output, size_t capacity, uint64_t& dropped) const noexcept {
    CriticalSectionGuard guard;
    const auto available = static_cast<size_t>(trace_sequence_ < TRACE_CAPACITY ? trace_sequence_ : TRACE_CAPACITY);
    const auto count = available < capacity ? available : capacity;
    dropped = trace_sequence_ - count;
    if (!output) return 0;
    for (size_t i = 0; i < count; ++i) output[i] = trace_[(trace_sequence_ - count + i) % TRACE_CAPACITY];
    return count;
}

void Scheduler::idle_task_func(void* /*arg*/) noexcept {
    while (true) {
        // In simulation, yield to give host CPU breathing room
        Port::yield();
    }
}

} // namespace myrtos
