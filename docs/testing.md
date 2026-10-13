# Building and testing the simulator foundation

The suite includes seven component cases, a check of the test harness, and a scheduler integration scenario. The integration scenario runs tasks on a worker thread while another thread samples telemetry and exchanges virtual I/O; it checks IPC, timeouts, returning tasks, and stop/restart. Further timer, mutex, and UI-control coverage is planned.

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

CTest runs seven component cases in separate processes, one test of the checking mechanism itself, and one scheduler integration case. Each process has a 10-second timeout, so a hung test becomes a failure rather than hanging indefinitely.

The `harness.rejects_failed_check` case deliberately executes `CHECK(false)`. It passes in CTest only when that process fails. This guards against accidentally disabling test checks in Release builds. It is not an eighth kernel feature test. Directly running `myrtos_tests --fail-check` is expected to exit with an error.

`CHECK` evaluates its expression once, counts it, and reports the file, line, and expression on failure. Unlike the old `assert` calls, it does not disappear when `NDEBUG` is defined. Running `myrtos_tests` without arguments executes all seven component cases and currently reports 84 checks in either build mode. A case name, such as `message_queue`, runs just that case; an unknown case returns an error.

`--no-tests=error` prevents an empty test suite from appearing successful. See the [CTest 3.20 command reference](https://cmake.org/cmake/help/v3.20/manual/ctest.1.html) and [expected-failure property](https://cmake.org/cmake/help/v3.20/prop_test/WILL_FAIL.html).

## Automation and Docker

`.github/workflows/build.yml` defines headless Debug/Release jobs on Windows and Ubuntu. It takes effect when the project is pushed to GitHub with Actions enabled; adding the file does not mean those remote jobs have already run. Windows jobs use the runner's default Visual Studio toolchain, while local Windows validation used MinGW.

The application Dockerfile now runs the registered CTest suite during image construction. Use `docker compose build` to build the browser-accessible application image, then `docker compose up` to run it. The scheduler integration case also runs here. Interactive controls require their own UI checks.

Validation on 2026-10-13: Windows MinGW GNU 16.2.0 and Linux GNU 13.3.0 passed all nine CTest cases in Debug and Release. Both platforms also passed the 120-frame dashboard smoke test with an advancing clock and clean shutdown. Linux builds used the existing local application image with networking disabled, a read-only source mount, and fresh builds in the container's temporary directory; the GUI ran under Xvfb. This verifies a relocated Linux build and rendering, but does not constitute a fresh application-image build, browser interaction test, or MSVC validation.

## Portable presets and dashboard smoke test

The checked-in CMake presets use paths relative to the checkout and discover tools from the current environment. With a C++20 compiler and Ninja on PATH, these commands are the same on Windows and Linux:

```sh
cmake --preset desktop-debug
cmake --build --preset desktop-debug --parallel 4
ctest --preset desktop-debug
```

Use `headless-debug` or `headless-release` in all three commands for the corresponding headless configuration. Generated build directories contain local absolute paths and should be regenerated on each machine. For personal compiler choices, use the ignored `CMakeUserPresets.json` or command-line CMake options. No particular drive, username, or compiler installation directory is required by the project.

To exercise the actual rendering loop and shutdown, run `myrtos_dashboard --smoke-test` from the generated desktop directory. It renders 120 frames, checks that the simulation clock advanced, requests stop, and joins the simulation thread. This requires an OpenGL display; Linux containers can provide one with Xvfb. An external test timeout should be used to detect a freeze. The normal invocation runs until you close the window.

The host runtime advances ticks using a monotonic clock on the scheduler thread at cooperative scheduling points. Stop is handled at those points too. Tasks that never yield, sleep, block, or return still require a separate execution strategy for forced interruption. Host stack measurements are displayed as unavailable: Windows fibers use OS-owned stacks, and sampling an executing Linux stack would be unsafe.

See [host-runtime.md](host-runtime.md) for thread ownership, locking, and shutdown rules when writing tasks.
