# Desktop execution model

The dashboard runs on the main application thread. A separate simulation thread initializes the scheduler, creates task contexts, and runs them. Windows uses fibers; Linux uses `ucontext`. Create tasks on the thread that initialized the scheduler. The current implementation supports one scheduler instance per process.

## Why the window froze

A task could switch away while holding the shared kernel lock. The dashboard then waited for that lock while collecting task information. On Linux, nested kernel operations could also deadlock because the old lock was not recursive.

The host runtime now shares a recursive lock across scheduler snapshots and virtual peripheral operations. Before switching contexts, the port releases the current context's lock depth. When that context resumes, the port restores its depth before returning to the suspended caller. Scheduler state changes for the transition must be complete before this boundary; do not introduce a context switch midway through an inconsistent data structure update.

The dashboard reads a copied task snapshot with its sampled tick. GPIO reads return values rather than references into mutable storage. GPIO, UART, and virtual interrupt operations use the same lock. Pending interrupt handlers execute on the simulation thread. UI code should use snapshots and peripheral APIs; raw task pointers and task scheduling APIs belong to the simulation thread.

## Time and cooperation

A monotonic host clock supplies nominal one-millisecond ticks. The simulation thread checks it at cooperative scheduling points and advances overdue ticks over successive polls. Idle execution waits for the next tick. There is no timer thread modifying the interrupt controller concurrently.

Task code must yield, sleep, block, or return so the scheduler can process time and stop requests. A task that spins forever can prevent shutdown. Desktop scheduling does not provide hard real-time deadlines, and timing varies with host load. Pause, single-step, and adjustable simulation speed still need execution-engine integration.

## Task lifetime and shutdown

A task that returns is removed from the ready queue and marked terminated. A timed-out wait is removed from its event queue before the task becomes ready. Task storage must remain valid until the scheduler stops. Host task stacks must be 16-byte aligned and at least 16 KiB; examples and the idle task use the 64 KiB default.

Closing the dashboard requests stop and joins the simulation thread before the application exits. At the next cooperative boundary, the port returns to the scheduler's saved starting context and releases host contexts. Reinitialization is required before starting another run.

Stopping discards suspended task stacks; it does not unwind their C++ automatic objects. Resources needing cleanup should be owned outside those stacks, or explicitly released by tasks before stopping. Task-return destructors run normally. Stop/restart coverage does not imply arbitrary task cancellation is safe.

Windows allocates fiber stacks independently of the supplied task buffer. Reading a live Linux stack from the UI would also be unsafe. The dashboard therefore shows stack measurements as unavailable.

## Portability

Checked-in CMake presets resolve output directories relative to the checkout and discover compiler tools from the environment. Personal compiler selections belong in command-line options or an ignored `CMakeUserPresets.json`. Regenerate build output on each machine; CMake caches and compiled binaries are not portable source artifacts.

The integration test runs communicating tasks while another thread samples telemetry and exchanges virtual I/O. It covers timeout cleanup, task return, and two start/stop cycles. The dashboard's `--smoke-test` mode additionally checks rendering, clock progress, and shutdown. See [testing.md](testing.md) for commands and the platforms actually validated.
