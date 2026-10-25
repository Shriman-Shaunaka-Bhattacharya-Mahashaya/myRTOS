# Building and testing the simulator

The suite contains 14 CTest cases: seven component cases, a harness check, a scheduler integration case, and five regression scenarios. These cover pause/step/reset/speed, UART and GPIO responses, timer lifetime, pool validation, mutex inheritance, queue deadlines across clock rollover, and live teaching-mode holds. The scheduler integration case also checks concurrent UI access and stop/restart.

## Build without a graphical environment

Install CMake 3.20 or newer, Ninja, and a C++20 compiler. Use a modern MinGW toolchain or a Visual Studio developer terminal on Windows; use GCC or Clang on Linux. The commands below explicitly select Ninja and let CMake select the compiler. If several compilers are installed, select the intended one explicitly or use its development terminal. For Windows setup and recovery from a cached generator, see the section below.

```sh
cmake -S . -B out/tests-debug -G Ninja -DMYRTOS_BUILD_UI=OFF -DMYRTOS_BUILD_EXAMPLES=OFF -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build out/tests-debug --config Debug
ctest --test-dir out/tests-debug -C Debug --output-on-failure --no-tests=error

cmake -S . -B out/tests-release -G Ninja -DMYRTOS_BUILD_UI=OFF -DMYRTOS_BUILD_EXAMPLES=OFF -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build out/tests-release --config Release
ctest --test-dir out/tests-release -C Release --output-on-failure --no-tests=error
```

Debug builds retain information useful for debugging. Release builds optimize the program; both must run the same checks. `--config`/`-C` select the configuration for multi-configuration generators such as Visual Studio; `CMAKE_BUILD_TYPE` selects it for generators such as Ninja or Unix Makefiles.

With UI disabled, configuration does not load FetchContent or require GLFW, ImGui, OpenGL, or a display. No additional test framework download is required. Build tools and the compiler must already be installed.

## Windows toolchain setup

The **compiler** translates C++ into machine code. The **build tool** runs compilation commands; `-G Ninja` tells CMake which build tool to use. Plain PowerShell does not necessarily have Visual Studio's NMake environment available.

Check which programs your terminal finds:

```powershell
Get-Command gcc, g++, ninja
g++ --version
```

For MinGW, prepend your own toolchain's `bin` directory to `PATH` for the current terminal:

```powershell
$toolchainBin = Read-Host "Full path to your MinGW-w64 bin directory"
$env:Path = "$toolchainBin;$env:Path"
g++ --version
ninja --version
```

The selected directory applies only to the current terminal. An older compiler earlier on `PATH`, such as MinGW GCC 6.3, cannot build this C++20 project. The same `-G Ninja` commands above work once the modern compiler and Ninja are selected.

If you see `nmake` missing, CMake selected the NMake generator. A build directory remembers its selected generator and compiler. For CMake 3.24 or newer, after selecting the intended toolchain, refresh the failed desktop configuration with:

```powershell
cmake --fresh -S . -B out/desktop -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build out/desktop --parallel 4
ctest --test-dir out/desktop --output-on-failure --no-tests=error
```

For older CMake versions, configure into a new build directory instead. The [CMake command reference](https://cmake.org/cmake/help/latest/manual/cmake.1.html#cmdoption-cmake-fresh) documents how `--fresh` recreates the generated cache and configuration files.

To use MSVC, start a Visual Studio developer terminal so its compiler environment is initialized; Ninja must also be available. Use a separate build directory when switching between MSVC and MinGW.

## Options

| CMake option | Default | Effect |
| --- | --- | --- |
| `MYRTOS_BUILD_UI` | `ON` | Builds the desktop dashboard and its graphics dependencies. |
| `MYRTOS_BUILD_EXAMPLES` | `ON` | Builds the command-line demo. The dashboard is controlled separately by the UI option. |
| `BUILD_TESTING` | `ON` | Builds and registers the tests. Set all three options OFF to build just the libraries. |

The default build still includes the existing dashboard. Enabling the UI uses local third-party sources when available, otherwise the existing dependency downloads. Linux UI builds also require the graphics development packages listed in the Dockerfile.

## Reading results

CTest runs each case in a separate process. Original cases have 10-second timeouts; the five additional regression scenarios have 20-second timeouts. A hung test becomes a failure rather than hanging indefinitely.

The `harness.rejects_failed_check` case deliberately executes `CHECK(false)`. It passes in CTest only when that process fails. This guards against accidentally disabling test checks in Release builds. It is not an eighth kernel feature test. Directly running `myrtos_tests --fail-check` is expected to exit with an error.

`CHECK` evaluates its expression once, counts it, and reports the file, line, and expression on failure. Unlike the old `assert` calls, it does not disappear when `NDEBUG` is defined. Running `myrtos_tests` without arguments executes all seven component cases and currently reports 84 checks in either build mode. A case name, such as `message_queue`, runs just that case; an unknown case returns an error.

`--no-tests=error` prevents an empty test suite from appearing successful. See the [CTest 3.20 command reference](https://cmake.org/cmake/help/v3.20/manual/ctest.1.html) and [expected-failure property](https://cmake.org/cmake/help/v3.20/prop_test/WILL_FAIL.html).

## Automation and Docker

`.github/workflows/build.yml` defines headless Debug/Release jobs on Windows and Ubuntu. It takes effect when the project is pushed to GitHub with Actions enabled; adding the file does not mean those remote jobs have already run. Windows jobs use the runner's default Visual Studio toolchain, while local Windows validation used MinGW.

The application Dockerfile now runs the registered CTest suite during image construction. Use `docker compose build` to build the browser-accessible application image, then `docker compose up` to run it. The scheduler integration case also runs here. Interactive controls require their own UI checks.

## Recorded validation

Latest teaching-mode validation on 2026-10-25:

| Check | Recorded result |
| --- | --- |
| Windows MinGW GNU 16.2.0, Debug and Release | All 14 CTest cases passed. |
| Linux GNU 13.3.0, Debug and Release | All 14 CTest cases passed. Release ran during Docker image construction; Debug ran in a disposable network-disabled container. |
| Windows dashboard smoke test | 120 frames, advancing clock and clean shutdown passed, including the final teaching-label update. |
| Browser through Edge/noVNC | Manual/reset at tick zero, Ready/Running (held)/Blocked snapshots, next-transition progression, automatic progression and Stop checked. Final label review confirmed Dispatch tick and Selected. |
| Earlier browser interaction checks | Clock pause/step, UART PING/PONG and LED commands, GPIO2-to-LED behavior, speed presets, reset/stop, resizing and CSV download checked. |

Earlier Linux dashboard smoke runs passed before teaching mode was added; the latest Linux GUI was exercised through the browser. These are local results, not a claim of remote CI success or local MSVC/other-browser qualification. No runtime tests were rerun for this documentation-only update.

For a repeatable interactive check with expected results, follow [demo-walkthrough.md](demo-walkthrough.md). CTest checks the shared execution engine; browser rehearsal additionally checks the display and input transport.

## Portable presets and dashboard smoke test

The checked-in CMake presets use paths relative to the checkout and discover tools from the current environment. With a C++20 compiler and Ninja on PATH, these commands are the same on Windows and Linux:

```sh
cmake --preset desktop-debug
cmake --build --preset desktop-debug --parallel 4
ctest --preset desktop-debug
```

Use `headless-debug` or `headless-release` in all three commands for the corresponding headless configuration. Generated build directories contain local absolute paths and should be regenerated on each machine. For personal compiler choices, use the ignored `CMakeUserPresets.json` or command-line CMake options. No particular drive, username, or compiler installation directory is required by the project.

To exercise the actual rendering loop and shutdown, run `myrtos_dashboard --smoke-test` from the generated desktop directory. It renders 120 frames, checks that the simulation clock advanced, requests stop, and joins the simulation thread. This requires an OpenGL display; Linux containers can provide one with Xvfb. An external test timeout should be used to detect a freeze. The normal invocation runs until you close the window.

The host runtime advances ticks using a monotonic clock on the scheduler thread at cooperative scheduling points. Stop is handled at those points too. Clock polling and pause acknowledgement occur at idle. Tasks must allow idle to run by sleeping or blocking; repeatedly yielding while remaining ready can still starve idle. Forced interruption requires a separate execution strategy. Host stack usage is not measured: Windows fibers use OS-owned stacks, and sampling an executing Linux stack would be unsafe.

See [host-runtime.md](host-runtime.md) for thread ownership, locking, and shutdown rules when writing tasks.

## Regression scenarios

The five `regression.*` cases have 20-second process timeouts; the seven component cases, harness case and scheduler integration case have 10-second timeouts.

| Case | What it checks |
| --- | --- |
| `regression.demo_integration` | Pause and exact clock step, UART command replies/errors and GPIO outputs, buffer rejection, speed comparison, fractional-speed timing down to 0.0005x, invalid/non-finite speed rejection, dispatch telemetry, repeated reset and prompt stop during a slow clock wait. |
| `regression.kernel_regressions` | List membership, pool layout and duplicate-release rejection, invalid blocking contexts, wrap-safe helpers, timer modes and callback/object lifetime. |
| `regression.mutex_integration` | Recursive locks, multiple held mutexes, chained inheritance, timed-out donations and ownership transfer. |
| `regression.deadline_integration` | Original queue deadlines under contention across rollover, sleep wakeups and lock cleanup when a task returns. |
| `regression.transition_integration` | Initial selection held at tick zero, live Ready/Running/Blocked snapshots, UART consumed only after release, one-dispatch manual progression, automatic delay, no catch-up after a long hold, and stop/reset before initial context entry and at later holds. |

To run only the teaching scenario after building:

```sh
ctest --test-dir out/desktop -R regression.transition_integration --output-on-failure --no-tests=error
```

For a multi-configuration generator, add `-C Debug` or `-C Release`. Clock tests use host-timing tolerances rather than requiring exact nanosecond scheduling. They verify the clock behavior and supported task contracts, not hardware real-time guarantees.

## Interactive acceptance check

1. With Teaching Off, Reset, Step, then send PING and step again. Confirm PONG and an exact one-tick increment per step.
2. Send LED ON/OFF, and toggle GPIO2 with a step after each action. Confirm console responses and pin1 state.
3. Try slow clock presets. Confirm the selected speed, sensible tick spacing and responsive Pause/Stop. A default sensor interval takes five minutes at 0.0005x; absence of rapid messages is expected.
4. Choose Manual, then Reset. Confirm Inputs is Running (held), other application tasks are Ready, and tick remains zero while waiting.
5. Advance through Producer, Consumer, Heartbeat and Idle using Next transition. Check that previous work becomes Blocked and that UART output appears only after its producing tasks run.
6. Choose Automatic and adjust Hold seconds. Confirm timed progression. Change to Off and verify normal clock Pause/Step become available without a teaching-time catch-up burst.
7. Export the trace and, in the browser, Download trace. Check CSV columns `sequence,tick,from_task,to_task`. A newest event can represent a teaching-held selection before execution resumes.
8. Stop while held. Confirm termination, then Reset to verify a fresh session. Close the native window or use `docker compose down` for the browser service.

The automated suite checks shared engine behavior. This rehearsal separately checks rendering, controls and browser input transport. See [demo-walkthrough.md](demo-walkthrough.md) for expected states and recovery commands.
