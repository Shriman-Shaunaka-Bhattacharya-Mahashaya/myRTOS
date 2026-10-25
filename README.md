# myRTOS: RTOS Simulator in C++

myRTOS is a custom RTOS simulator written in C++20 and built with CMake. It provides a place to develop and explore task scheduling, communication between tasks, and virtual hardware through a graphical dashboard.

The project focuses on Windows and Linux, with a Docker configuration for accessing the desktop dashboard through a browser. The dashboard provides pause/resume, exact single-tick stepping, adjustable speed, stop/reset, UART commands, GPIO responses, and recorded scheduler events. See the [demonstration walkthrough](docs/demo-walkthrough.md).

## What the simulator contains

| Component | Purpose |
| --- | --- |
| Scheduler | Organizes tasks into 32 priority levels and selects the highest-priority ready task using a bitmap. Lower numbers mean higher priority. |
| Task control blocks | Store each task's name, state, priority, context, and diagnostic data. |
| Semaphores | Provide counters that tasks can use to signal events. |
| Mutexes | Coordinate access to shared resources, with priority-inheritance logic. |
| Message queues | Transfer fixed-size messages between tasks. |
| Memory pools | Provide storage in fixed-size blocks. |
| Software timers | Provide timer objects and callback processing. |
| Virtual peripherals | Model 16 interrupt lines, eight digital GPIO pins, and UART receive/transmit buffers. |
| Dashboard | Presents task information, sampled execution activity, GPIO controls, and a UART console using Dear ImGui and GLFW. |
| Tests | Check individual components through CTest in Debug and Release builds. |

The [demonstration walkthrough](docs/demo-walkthrough.md) connects these components in a repeatable input/output scenario.

## Understanding tasks and simulated time

An RTOS organizes work into **tasks**. A task might collect sensor readings, process messages, or toggle an LED. The **scheduler** decides which ready task runs according to its priority. A task can wait for an event or sleep for a specified number of ticks while other tasks run.

A **tick** represents a unit of time inside the simulation. The examples use a nominal one-millisecond tick. The host operating system controls when the application receives CPU time, so simulated ticks and elapsed real time are separate concepts.

The Windows port uses Win32 fibers, and the Linux port uses `ucontext`. A shared host runtime uses a monotonic clock to advance ticks on the simulation thread. Both preserve task execution contexts so tasks can resume where they left off. Pending virtual interrupts and clock ticks are processed when idle polls the host runtime. Application tasks must sleep or block often enough to let idle run; repeatedly yielding while remaining ready can still starve it.

## Build and test without the UI

Start with this workflow to build the core components and run the automated tests. It requires:

- CMake 3.20 or newer.
- A C++20 compiler, such as a modern MinGW-w64 toolchain on Windows or GCC/Clang on Linux.
- Ninja, the build tool used by the commands below.

On Windows, ensure that the intended compiler and Ninja are on `PATH` before configuring. If several compiler installations exist, follow [Windows toolchain setup](docs/testing.md#windows-toolchain-setup) to select a modern toolchain.

Run these commands from the repository root:

```sh
cmake -S . -B out/tests-debug -G Ninja -DMYRTOS_BUILD_UI=OFF -DMYRTOS_BUILD_EXAMPLES=OFF -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build out/tests-debug --config Debug
ctest --test-dir out/tests-debug -C Debug --output-on-failure --no-tests=error
```

To test an optimized Release build:

```sh
cmake -S . -B out/tests-release -G Ninja -DMYRTOS_BUILD_UI=OFF -DMYRTOS_BUILD_EXAMPLES=OFF -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build out/tests-release --config Release
ctest --test-dir out/tests-release -C Release --output-on-failure --no-tests=error
```

CMake selects a compiler from your environment; `-G Ninja` explicitly selects the build tool. If multiple toolchains are installed, run from the intended toolchain's terminal or specify the compiler explicitly. Other generators are supported when their build tools and compiler environments are available.

Headless builds skip the graphics dependencies and work without a display. See [docs/testing.md](docs/testing.md) for compiler selection, build configurations, and test details.

### What the tests cover

CTest currently registers fourteen cases:

- Seven component cases: intrusive lists, synthetic stack markers, priority bitmap selection, memory pool operations, nonblocking message queues, GPIO/interrupt dispatch, and UART buffers.
- One expected-failure case that verifies a failed test check produces a failing process.
- One scheduler integration case covering task communication, timeout cleanup, returning tasks, responsive telemetry, and stop/restart.
- Five regression scenarios covering demo controls and input/output, timer/pool/list validation, mutex inheritance, queue deadlines across tick rollover, and live transition teaching controls.

The checks remain active in Debug and Release builds. The original nine cases have ten-second timeouts; the five regression scenarios have twenty-second timeouts. Running `myrtos_tests` directly executes the seven component cases and currently reports 84 evaluated checks.

The regression scenarios exercise the same session used by the UI. See [testing.md](docs/testing.md) for test scope and platform validation.

Headless Debug and Release tests have passed on Windows with MinGW and on Linux inside a Docker container. The GitHub Actions workflow in [.github/workflows/build.yml](.github/workflows/build.yml) defines Windows and Ubuntu build/test jobs.

## Build the desktop dashboard

The desktop build uses Dear ImGui, GLFW, and OpenGL. CMake uses sources in `third_party/` when available and otherwise downloads the UI libraries during configuration. Git and network access are required for those downloads.

On Linux, install the compiler and graphics development packages for your distribution. The [Dockerfile](Dockerfile) provides the package list used by the Ubuntu container.

From the repository root, with Ninja and a C++20 compiler available:

```sh
cmake --preset desktop-debug
cmake --build --preset desktop-debug --parallel 4
ctest --preset desktop-debug
```

These presets work on Windows and Linux and use checkout-relative output paths. Toolchains are selected from your environment. Keep personal compiler paths in command-line options or an ignored `CMakeUserPresets.json`; regenerate the build directory when moving between machines.

If this directory was previously configured with NMake or another generator, use a fresh build directory or use `cmake --fresh --preset desktop-debug` with CMake 3.24 or newer. This refreshes generated configuration, allowing Ninja to be selected. Apply the Windows toolchain setup above first.

By default, this builds the component tests, command-line demo, and graphical dashboard.

For Ninja or Makefile builds, launch the dashboard with:

**Windows (PowerShell):**

```powershell
.\out\desktop\myrtos_dashboard.exe
```

**Linux:**

```sh
./out/desktop/myrtos_dashboard
```

Visual Studio and other multi-configuration generators place executables in a configuration subdirectory. For example, a Windows Debug build uses:

```powershell
.\out\desktop\Debug\myrtos_dashboard.exe
```

### Explore the dashboard

The example application creates Producer, Consumer, Heartbeat, and Inputs tasks. Its dashboard provides:

- A task table showing states, priorities, context-switch counts, last-dispatch ticks, and recent-activity highlights.
- A bounded record of actual scheduler switches, with CSV export.
- HIGH/LOW buttons for virtual input pins and indicators for pin states.
- A UART output area showing text produced by tasks.
- A text field that injects data into the virtual UART receive buffer.

**GPIO** means simple digital input/output signals, such as a button or LED. **UART** represents serial communication; here it uses in-memory buffers instead of physical wires. Application tasks and interrupt handlers determine how injected inputs are processed.

Choose 0.0005x (one tick every two seconds), 0.001x (one tick per second), or 0.005x (five ticks per second) for very slow inspection. Choose 0.01x in the Speed dropdown for approximately ten ticks per second. Task rows briefly turn green after execution; Dispatch tick shows when the scheduler last selected a task. Blocked means waiting, and Idle is labeled Waiting clock or Paused to explain its role.

Pause waits for worker acknowledgement. Step advances exactly one tick while paused. Reset starts a fresh paused run. Send `PING` for `PONG`, `LED ON`/`LED OFF` to control pin 1, or `HELP` for commands. Toggling input pin 2 changes pin 1 and produces a console response after step or resume. Pin 0 remains the heartbeat.

### Watch the scheduler in slow motion

Choose **Teaching mode: Manual**, then **Reset**. The selected task stays **Running (held)** until you click **Next transition**. Other tasks show their committed Ready/Blocked states. Choose **Automatic** and a hold duration to advance without clicking, or **Off** to return to normal clock controls. Reset preserves teaching mode. The UI remains responsive and Stop releases a held worker.

These are live execution holds at committed task-switch boundaries, including startup. Teaching pauses do not advance simulation time or build up overdue ticks. For a first demonstration, use 1x clock speed and the default two-second hold. See the [walkthrough](docs/demo-walkthrough.md#live-scheduler-teaching-mode).

### Command-line demo

The `myrtos_sim` executable runs the example tasks and prints UART output in the terminal:

```powershell
# Windows, Ninja or Makefile build
.\out\desktop\myrtos_sim.exe
```

```sh
# Linux
./out/desktop/myrtos_sim
```

For a multi-configuration build, include the configuration subdirectory. Press `Ctrl+C` to stop the terminal demo.

## Run through Docker and a browser

Install Docker with Compose support and start its engine. On Windows, use the Linux-container environment. Docker supplies the Linux build tools and graphical environment inside the container.

From the repository root:

```sh
docker compose up --build
```

The Dockerfile builds the application and runs its registered CTest suite. The startup script launches a virtual display and the desktop dashboard. Open:

```text
http://localhost:8080/
```

The page connects automatically and scales the virtual desktop to the browser window. Click Export trace inside the desktop, then Download trace in the page header to save the recent scheduler events as CSV. Compose exposes it on localhost only; the container runs as an unprivileged user and supervises its display and network processes.

The browser displays and controls the desktop application through this pipeline:

```text
myRTOS dashboard -> Xvfb virtual display -> x11vnc -> websockify -> noVNC browser client
```

Xvfb creates an in-memory display, Openbox manages its windows, and the VNC services carry screen updates and keyboard/mouse input between the container and browser. The initial build downloads the base image, system packages, and UI dependencies as needed.

While the Compose service is running, execute tests in the container with:

```sh
docker compose exec myrtos ctest --test-dir /app/build --output-on-failure --no-tests=error
```

Stop and remove the Compose service with:

```sh
docker compose down
```

## Build options

| Option | Default | Controls |
| --- | --- | --- |
| `MYRTOS_BUILD_UI` | `ON` | Dashboard and graphics dependencies. |
| `MYRTOS_BUILD_EXAMPLES` | `ON` | Command-line demo. |
| `BUILD_TESTING` | `ON` | Test executable and CTest registration. |

Set all three options to `OFF` to build only the project libraries.

## Project structure

| Path | Contents |
| --- | --- |
| `kernel/` | Scheduler, task records, communication primitives, memory pools, and timers. |
| `port/` | Windows/Linux context switching and host timing support. |
| `hw/` | Virtual interrupt controller, GPIO, and UART. |
| `ui/` | Dashboard rendering and input widgets. |
| `demo/` | Shared application session, task ownership, command parser, and input/output scenario. |
| `examples/` | Graphical and command-line application entry points. |
| `tests/` | Component, integration and regression cases, with an always-active test checker. |
| `web/` | Browser wrapper embedding the desktop and providing trace downloads. |
| `docs/` | Build and testing documentation. |
| `.github/workflows/` | Continuous-integration configuration. |
| `CMakeLists.txt` | Native build targets and options. |
| `Dockerfile`, `docker-compose.yml`, `entrypoint.sh` | Container build, launch configuration, and virtual desktop startup. |

The kernel contains scheduling and communication logic; the port supplies host execution support; virtual peripherals model inputs and outputs; the UI presents information and accepts interaction.

## Execution scope

The supported demo uses cooperative host tasks. Custom task code must allow idle to run; the simulator cannot forcibly interrupt an infinite loop. Stop releases task contexts without unwinding suspended C++ stacks, so persistent resources belong outside task-local scopes. Host stack utilization is not measured, and tick samples are not exact CPU-time measurements.

Finite waits are limited to 2,147,483,647 ticks to support wrap-safe deadline comparisons. Priority 31 is reserved for idle. Pools use constant-time allocation and validated linear-time release. Queues accept trivially copyable message types. Timer callbacks execute during tick processing and must not block.

The trace is bounded; arbitrary user-defined scenario replay and embedded hardware ports are separate extensions. The [demo walkthrough](docs/demo-walkthrough.md) defines the supported scenario and its expected results.

## Documentation

See [docs/testing.md](docs/testing.md) for build commands, test behavior, and validation results, and [docs/host-runtime.md](docs/host-runtime.md) for task execution and shutdown rules.

In teaching mode, Dispatch tick and switch counts record scheduler selection before the held target resumes. The Activity cell says Selected for that held target; it does not claim that the target has already executed its task body.
