# Building and testing the simulator foundation

The tests currently check seven components, not complete running-task behavior. Scheduler blocking, timer correctness, concurrency, and UI interaction still need integration tests and fixes.

## Build without a graphical environment

Install CMake 3.20 or newer and a C++20 compiler. Use a modern MinGW toolchain or a Visual Studio developer terminal on Windows; use GCC or Clang on Linux. The commands below let CMake select the installed compiler/build tool. If several compilers are installed, select the intended one explicitly or use its development terminal.

```sh
cmake -S . -B out/tests-debug -DMYRTOS_BUILD_UI=OFF -DMYRTOS_BUILD_EXAMPLES=OFF -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build out/tests-debug --config Debug
ctest --test-dir out/tests-debug -C Debug --output-on-failure --no-tests=error

cmake -S . -B out/tests-release -DMYRTOS_BUILD_UI=OFF -DMYRTOS_BUILD_EXAMPLES=OFF -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build out/tests-release --config Release
ctest --test-dir out/tests-release -C Release --output-on-failure --no-tests=error
```

Debug builds retain information useful for debugging. Release builds optimize the program; both must run the same checks. `--config`/`-C` select the configuration for multi-configuration generators such as Visual Studio; `CMAKE_BUILD_TYPE` selects it for generators such as Ninja or Unix Makefiles.

With UI disabled, configuration does not load FetchContent or require GLFW, ImGui, OpenGL, or a display. No additional test framework download is required. Build tools and the compiler must already be installed.

## Options

| CMake option | Default | Effect |
| --- | --- | --- |
| `MYRTOS_BUILD_UI` | `ON` | Builds the desktop dashboard and its graphics dependencies. |
| `MYRTOS_BUILD_EXAMPLES` | `ON` | Builds the command-line demo. The dashboard is controlled separately by the UI option. |
| `BUILD_TESTING` | `ON` | Builds and registers the tests. Set all three options OFF to build just the libraries. |

The default build still includes the existing dashboard. Enabling the UI uses local third-party sources when available, otherwise the existing dependency downloads. Linux UI builds also require the graphics development packages listed in the Dockerfile.

## Reading results

CTest runs seven component cases in separate processes plus one test of the checking mechanism itself. Each process has a 10-second timeout, so a hung test becomes a failure rather than hanging indefinitely.

The `harness.rejects_failed_check` case deliberately executes `CHECK(false)`. It passes in CTest only when that process fails. This guards against accidentally disabling test checks in Release builds. It is not an eighth kernel feature test. Directly running `myrtos_tests --fail-check` is expected to exit with an error.

`CHECK` evaluates its expression once, counts it, and reports the file, line, and expression on failure. Unlike the old `assert` calls, it does not disappear when `NDEBUG` is defined. Running `myrtos_tests` without arguments executes all seven component cases and currently reports 84 checks in either build mode. A case name, such as `message_queue`, runs just that case; an unknown case returns an error.

`--no-tests=error` prevents an empty test suite from appearing successful. See the [CTest 3.20 command reference](https://cmake.org/cmake/help/v3.20/manual/ctest.1.html) and [expected-failure property](https://cmake.org/cmake/help/v3.20/prop_test/WILL_FAIL.html).

## Automation and Docker

`.github/workflows/build.yml` defines headless Debug/Release jobs on Windows and Ubuntu. It takes effect when the project is pushed to GitHub with Actions enabled; adding the file does not mean those remote jobs have already run. Windows jobs use the runner's default Visual Studio toolchain, while local Windows validation used MinGW.

The application Dockerfile now runs the registered CTest suite during image construction. Use `docker compose build` to build the browser-accessible application image, then `docker compose up` to run it. Passing component tests does not yet establish that the interactive scheduler or UI is correct.

Validation on 2026-10-13: Windows MinGW GNU 16.2.0 and Linux GNU 13.3.0 passed all eight CTest cases in Debug and Release. Linux was tested inside the existing local application image with networking disabled, the source mounted read-only, and fresh builds in the container's temporary directory. This validates the headless Linux build; it is not a fresh application-image build or a GUI runtime test.
