# Desktop execution model

The dashboard runs on the main application thread. A separate simulation thread initializes the scheduler, creates task contexts, and runs them. Windows uses fibers; Linux uses `ucontext`. Create tasks on the thread that initialized the scheduler. The current implementation supports one scheduler instance per process.

## Why the window froze

A task could switch away while holding the shared kernel lock. The dashboard then waited for that lock while collecting task information. On Linux, nested kernel operations could also deadlock because the old lock was not recursive.

The host runtime now shares a recursive lock across scheduler snapshots and virtual peripheral operations. Before switching contexts, the port releases the current context's lock depth. When that context resumes, the port restores its depth before returning to the suspended caller. Scheduler state changes for the transition must be complete before this boundary; do not introduce a context switch midway through an inconsistent data structure update.

The dashboard reads a copied task snapshot with its sampled tick. GPIO reads return values rather than references into mutable storage. GPIO, UART, and virtual interrupt operations use the same lock. Pending interrupt handlers execute on the simulation thread. UI code should use snapshots and peripheral APIs; raw task pointers and task scheduling APIs belong to the simulation thread.

## Time and cooperation

A monotonic host clock supplies nominal one-millisecond ticks. The simulation thread checks it at idle and advances overdue ticks over successive polls. Idle execution waits for the next tick. There is no timer thread modifying the interrupt controller concurrently.

Task code must allow idle to run by sleeping or blocking so the runtime can process time and stop requests. Yielding repeatedly while remaining ready can still starve idle. A task that spins forever can prevent shutdown. Desktop scheduling does not provide hard real-time deadlines, and timing varies with host load.

Pause is acknowledged at idle after ready work settles. Step allows exactly one tick and the work that becomes ready, then pauses again. Speed supports 0.0005x through 10x and rebases the next deadline when changed. Stop wakes a paused worker. With teaching Off, dashboard Reset stops and joins the worker, replaces its state, and begins a new clock-paused run at tick zero. With teaching enabled, Reset preserves that mode and holds or delays the first dispatch instead. Every new run restores speed to 1x. The dashboard and command-line example share the same demo session.

## Task lifetime and shutdown

A task that returns is removed from the ready queue and marked terminated. A timed-out wait is removed from its event queue before the task becomes ready. Task storage must remain valid until the scheduler stops. Host task storage must be 16-byte aligned and large enough for the task record plus at least 16 KiB of stack; examples and the idle task use the 64 KiB default.

Closing the dashboard requests stop and joins the simulation thread before the application exits. At the next cooperative boundary, the port returns to the scheduler's saved starting context and releases host contexts. Reinitialization is required before starting another run.

Stopping discards suspended task stacks; it does not unwind their C++ automatic objects. Resources needing cleanup should be owned outside those stacks, or explicitly released by tasks before stopping. Task-return destructors run normally. Stop/restart coverage does not imply arbitrary task cancellation is safe.

Windows allocates fiber stacks independently of the supplied task buffer. Reading a live Linux stack from the UI would also be unsafe. The dashboard therefore omits stack usage measurements. Tick samples are observations, not precise CPU-time measurements.

## Portability

Checked-in CMake presets resolve output directories relative to the checkout and discover compiler tools from the environment. Personal compiler selections belong in command-line options or an ignored `CMakeUserPresets.json`. Regenerate build output on each machine; CMake caches and compiled binaries are not portable source artifacts.

The integration test runs communicating tasks while another thread samples telemetry and exchanges virtual I/O. It covers timeout cleanup, task return, and two start/stop cycles. The dashboard's `--smoke-test` mode additionally checks rendering, clock progress, and shutdown. See [testing.md](testing.md) for commands and the platforms actually validated.

## Kernel contracts

Finite waits are limited to half the 32-bit tick range so deadline comparisons remain valid across rollover. Queue retries preserve their original deadline. Timer callbacks run under the scheduler lock and must not block; one-shot timers deactivate before their callback, and callbacks may stop, reset or destroy a timer.

Mutex priority inheritance accounts for every held mutex and propagates along wait chains. Returning tasks release held mutexes. Stop removes wait-list membership and ownership before freeing task stacks. Intrusive list nodes track their owning list to reject duplicate insertion. Fixed-block pools validate alignment and reject duplicate releases; allocation is constant time, while validated release scans the free list.

The scheduler retains 2,048 committed dispatch events, including startup selection. The event is recorded before context handoff, so a teaching-held target can already appear in the trace. The dashboard displays and exports the latest 512 and reports how many older events are omitted. UART submissions are accepted as complete packets or rejected; the demo consumes newline-terminated commands and handles GPIO2 interrupts on the simulation thread. See [demo-walkthrough.md](demo-walkthrough.md) for commands and expected output.

## Teaching dispatch boundaries

The host ports invoke a transition gate after the scheduler has selected its next task, updated task states and recorded the switch, and after the port suspends all host critical-section depth. The gate releases the worker through a condition-variable notification (Manual) or an adjustable wall-time deadline (Automatic). Its metadata identifies the held sequence, source, target and tick. The UI calls the target Running (held), meaning selected but not yet handed execution.

The first dispatch is gated before entering a task context. A stop at that point skips context entry and follows normal cleanup; later stops return to the saved main context. No gate waits while holding the kernel mutex. Mode changes, duration changes and stop notify the same condition variable. Next transition grants one permit only while a manual gate is waiting; it does not queue future steps. Clock pause/step and teaching controls are mutually exclusive. Reset configures teaching before scheduler startup and preserves UI-selected mode/duration.

After a teaching hold, the next host tick deadline is rebased. The hold duration does not contribute simulated elapsed time. The gate observes committed dispatches, not arbitrary instructions or intermediate mutations. A task that never switches can still prevent teaching or stop service.

## Clock controls and teaching controls

| Operation | Teaching Off | Manual teaching | Automatic teaching |
| --- | --- | --- | --- |
| Clock Pause/Resume | Acknowledged at idle | Disabled in UI; pause API ignores requests | Disabled in UI; pause API ignores requests |
| Step 1 tick | Available after acknowledged pause | Unavailable | Unavailable |
| Next transition | Unavailable | Releases the current held selection once | Unavailable |
| Speed | Sets tick interval | Still sets tick interval between dispatches | Still sets tick interval between dispatches |
| Reset in dashboard | New paused run at tick zero | New run held at its first selection | New run delaying its first selection |
| Stop | Wakes clock wait and joins | Wakes transition hold and joins | Wakes timed hold and joins |

Speed accepts finite multipliers from 0.0005 to 10. The nominal interval is one millisecond divided by the multiplier. Invalid, NaN and infinite values are rejected. The UI exposes preset values listed in the [walkthrough](demo-walkthrough.md#choosing-a-clock-speed). Transition delay accepts 100 to 10,000 milliseconds and defaults to 2,000. Changing mode or duration while held starts a fresh timed deadline when automatic timing applies. Switching teaching Off releases a hold and resumes normal clock progression; it does not request a clock pause.

The session API defaults to teaching Off and an unpaused start. The dashboard explicitly requests clock pause on Reset when teaching is Off. Code starting a teaching run can pass mode and duration to `DemoSession::start` or `reset`; those controls are configured before the first scheduler dispatch. Pause configuration is ignored in favor of teaching mode when teaching is enabled.

## Reading telemetry correctly

Task state, switch count and Dispatch tick come from protected snapshots. The transition metadata is a separate protected read, so UI observations taken during an advancing run need not represent one combined instant. A held snapshot remains stable while inspected unless permitted input affects peripheral state.

Switch count and Dispatch tick record selection, including a task held before its first instruction. Running (held) describes that selected target. The Selected activity cell identifies the held target; the brief Ran recently hint is based on changes to dispatch count observed by the UI. Neither is a precise measurement of CPU execution duration. Task tick samples remain available through telemetry, but are not the dashboard's dispatch column. Host stack usage is not measured.

See the [task display guide](demo-walkthrough.md#reading-the-task-table) for user-facing labels and the [test guide](testing.md) for checked scenarios and current validation scope.
