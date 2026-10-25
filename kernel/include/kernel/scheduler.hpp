#pragma once

#include "kernel/types.hpp"
#include "kernel/list.hpp"
#include "kernel/tcb.hpp"

namespace myrtos {

constexpr size_t MAX_TASKS = 64;

/**
 * @brief Telemetry snapshot for UI and diagnostic logging.
 */
struct SwitchEvent { uint64_t sequence{0}; TickType tick{0}; TaskId from{0}; TaskId to{0}; };
struct TaskMetrics {
    TaskId id{INVALID_TASK_ID};
    char name[TASK_NAME_MAX_LEN]{};
    TaskState state{TaskState::Ready};
    Priority base_priority{IDLE_TASK_PRIORITY};
    Priority current_priority{IDLE_TASK_PRIORITY};
    bool stack_metrics_available{false};
    size_t stack_size{0};
    size_t stack_free_bytes{0};
    float stack_usage_percent{0.0f};
    uint64_t runtime_ticks{0};
    uint32_t context_switch_count{0};
    TickType last_run_tick{0}; // Tick at the most recent dispatch; count=0 means never run.
};

/**
 * @brief Priority-based bitmap scheduler with cooperative host execution.
 */
class Scheduler {
public:
    static Scheduler& instance() noexcept;

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    /// Initializes scheduler structures, queues, and the system idle task.
    Status init() noexcept;

    /// Creates and registers a new task with statically allocated stack memory.
    Status create_task(
        const char* name,
        TaskFunction function,
        void* argument,
        Priority priority,
        uint8_t* stack_memory,
        size_t stack_size,
        TaskId* out_task_id = nullptr
    ) noexcept;

    /// Starts the scheduler (transfers execution to the highest priority task).
    Status start() noexcept;

    /// Removes a returned task from scheduling. Called by the host task thunk.
    void finish_current_task() noexcept;
    void cleanup_tasks() noexcept;

    /// Checks if scheduler has started and is actively dispatching.
    [[nodiscard]] bool is_running() const noexcept;

    /// Voluntary context switch request from running task.
    void yield() noexcept;

    /// Called by the hardware/simulated timer interrupt on each tick.
    void tick_isr() noexcept;

    /// Puts the currently running task to sleep for a specified duration in ticks.
    Status sleep_ticks(TickType ticks) noexcept;

    /// Suspends a task, removing it from Ready/Delayed queues.
    Status suspend_task(TaskId id) noexcept;

    /// Resumes a previously suspended task.
    Status resume_task(TaskId id) noexcept;

    /// Temporarily raises or lowers a task's effective priority (Used by Mutex PIP).
    void change_task_priority(TaskControlBlock* task, Priority new_priority) noexcept;

    /// Blocks the current task on an event queue (e.g. semaphore, mutex, queue).
    Status block_current_task(IntrusiveList& wait_queue, TickType timeout_ticks) noexcept;

    /// Unblocks a task from an event queue and places it back in the Ready queue.
    void unblock_task(TaskControlBlock* task, Status result = Status::Ok) noexcept;

    /// Locks scheduler preemption (interrupts remain active, but task switches are deferred).
    void lock() noexcept;

    /// Unlocks scheduler preemption.
    void unlock() noexcept;

    bool can_block() const noexcept;
    Status set_initial_tick(TickType tick) noexcept;
    size_t get_trace(SwitchEvent* output, size_t capacity, uint64_t& dropped) const noexcept;
    [[nodiscard]] TaskControlBlock* current_task() const noexcept { return current_task_; }
    [[nodiscard]] TickType tick_count() const noexcept;
    [[nodiscard]] size_t task_count() const noexcept;

    /// Fills buffer with active task metrics for UI telemetry visualization.
    size_t get_task_metrics(TaskMetrics* out_buffer, size_t max_count, TickType* sampled_tick = nullptr) const noexcept;

private:
    friend class Mutex;
    void record_switch(TaskId from, TaskId to) noexcept;
    static constexpr size_t TRACE_CAPACITY = 2048;
    SwitchEvent trace_[TRACE_CAPACITY]{};
    uint64_t trace_sequence_{0};
    Scheduler() noexcept;

    /// Evaluates highest-priority ready task and switches context if necessary.
    void schedule() noexcept;

    /// Ready queue management
    void make_ready(TaskControlBlock* task) noexcept;
    void remove_from_ready(TaskControlBlock* task) noexcept;

    /// Idle task function
    static void idle_task_func(void* arg) noexcept;

    // Ready table: One list per priority (0..31)
    IntrusiveList ready_queues_[MAX_PRIORITIES]{};
    uint32_t ready_bitmap_{0}; // Bit P is set if ready_queues_[P] has at least one task

    // Delayed tasks scanned with wrap-safe deadline comparisons
    IntrusiveList delayed_list_{};

    // Suspended tasks
    IntrusiveList suspended_list_{};

    // Registry of all active tasks
    TaskControlBlock* task_registry_[MAX_TASKS]{};
    size_t task_count_{0};
    uint32_t next_task_id_{1};

    TaskControlBlock* current_task_{nullptr};
    TaskControlBlock* next_task_{nullptr};

    // Dedicated Idle Task
    TaskControlBlock idle_tcb_{};
    alignas(16) uint8_t idle_stack_[DEFAULT_STACK_SIZE_BYTES]{};

    TickType current_tick_{0};
    uint32_t lock_nesting_{0};
    bool is_running_{false};
    bool reschedule_pending_{false};
};

} // namespace myrtos
