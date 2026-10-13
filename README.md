# myRTOS: RTOS Simulator in C++

myRTOS is a custom RTOS simulator written in C++20 and built with CMake. It provides a place to develop and explore task scheduling, communication between tasks, and virtual hardware through a graphical dashboard.

The project focuses on Windows and Linux, with a Docker configuration for accessing the desktop dashboard through a browser. Development is progressing from tested components toward a complete interactive simulation environment.

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

These components are the foundation for the simulator's ongoing integration work. The [development roadmap](#development-roadmap) describes the next capabilities being completed.

## Understanding tasks and simulated time

An RTOS organizes work into **tasks**. A task might collect sensor readings, process messages, or toggle an LED. The **scheduler** decides which ready task runs according to its priority. A task can wait for an event or sleep for a specified number of ticks while other tasks run.

A **tick** represents a unit of time inside the simulation. The examples use a nominal one-millisecond tick. The host operating system controls when the application receives CPU time, so simulated ticks and elapsed real time are separate concepts.

The Windows port uses Win32 fibers, and the Linux port uses `ucontext`. A shared host runtime uses a monotonic clock to advance ticks on the simulation thread. Both preserve task execution contexts so tasks can resume where they left off. Pending virtual interrupts are processed at scheduling points in the host port; task code needs to cooperate by reaching those points.

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

CTest currently registers nine cases:

- Seven component cases: intrusive lists, synthetic stack markers, priority bitmap selection, memory pool operations, nonblocking message queues, GPIO/interrupt dispatch, and UART buffers.
- One expected-failure case that verifies a failed test check produces a failing process.
- One scheduler integration case covering task communication, timeout cleanup, returning tasks, responsive telemetry, and stop/restart.

The checks remain active in Debug and Release builds. Each case has a ten-second timeout. Running `myrtos_tests` directly executes the seven component cases and currently reports 84 evaluated checks.

These tests cover individual components and a running-task scenario. Broader mutex, timer, overload, and interactive-control tests are part of the roadmap.

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

The example application creates Producer, Consumer, and Heartbeat tasks. Its dashboard provides:

- A task table showing states, priorities, and context-switch counts from a consistent snapshot. Host stack measurements are marked unavailable.
- A rolling view of task activity sampled by the UI.
- HIGH/LOW buttons for virtual input pins and indicators for pin states.
- A UART output area showing text produced by tasks.
- A text field that injects data into the virtual UART receive buffer.

**GPIO** means simple digital input/output signals, such as a button or LED. **UART** represents serial communication; here it uses in-memory buffers instead of physical wires. Application tasks and interrupt handlers determine how injected inputs are processed.

Simulation controls and complete input-to-output scenarios are being connected to the execution engine as described in the roadmap below.

### Command-line demo

The `myrtos_sim` executable runs the example tasks and prints task information and UART output in the terminal:

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
http://localhost:8080/vnc.html
```

Use the noVNC page's **Connect** control to access the virtual desktop.

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
| `examples/` | Graphical and command-line application entry points. |
| `tests/` | Component cases and the always-active test checker. |
| `docs/` | Build and testing documentation. |
| `.github/workflows/` | Continuous-integration configuration. |
| `CMakeLists.txt` | Native build targets and options. |
| `Dockerfile`, `docker-compose.yml`, `entrypoint.sh` | Container build, launch configuration, and virtual desktop startup. |

The kernel contains scheduling and communication logic; the port supplies host execution support; virtual peripherals model inputs and outputs; the UI presents information and accepts interaction.

## Development roadmap

The next stages focus on:

1. A controllable simulation clock and tests that exercise running tasks.
2. Safe task switching, blocking, timeouts, lifecycle transitions, and communication between the UI and simulation.
3. Timer, priority-inheritance, memory-pool, and resource-lifecycle correctness.
4. Connected pause/resume, single-step, speed, reset, and stop controls.
5. Input-driven examples, recorded event traces, and repeatable scenarios.
6. Broader native UI validation on Windows/Linux and container/browser validation.

## Documentation

See [docs/testing.md](docs/testing.md) for build commands, test behavior, and validation results, and [docs/host-runtime.md](docs/host-runtime.md) for task execution and shutdown rules.
