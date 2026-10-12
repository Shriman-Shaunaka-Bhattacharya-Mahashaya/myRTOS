# myRTOS: Problem & Defect Diagnostic Log

> [!NOTE]
> This log records all problems, bugs, architectural blockers, and build issues encountered during the lifecycle of myRTOS.
> Each new problem is appended to the end of this document, keeping previous entries untouched.

---

## [PROBLEM-20261012-01]: Legacy MinGW 6.3.0 Toolchain and Missing CMake in System PATH
- **Component / Subsystem:** Host Build Environment & Toolchain (`Phase 0`)
- **Symptom & Failure Mode:**
  1. Invoking `cmake` failed with `CommandNotFoundException` (program not found in `PATH`).
  2. Invoking `g++ -std=c++20` failed with:
     ```
     g++.exe: error: unrecognized command line option '-std=c++20'; did you mean '-std=c++03'?
     ```
- **Root Cause Analysis:**
  The pre-existing compiler in `C:\MinGW\bin` is 32-bit legacy MinGW.org GCC 6.3.0 (dated 2016). It predates the C++20 standard by four years and targets 32-bit `mingw32`. CMake was not installed or configured in the user's system `PATH`.
- **Diagnostic Steps:**
  1. Ran `where.exe gcc` and `where.exe cmake` to identify executable paths.
  2. Ran `g++ -v` which revealed: `Target: mingw32`, `gcc version 6.3.0 (MinGW.org GCC-6.3.0-1)`.
  3. Ran test dialect probes (`-std=c++20`, `-std=c++17`, `-std=c++14`). Confirmed `-std=c++20` is unrecognized.
  4. Scanned system package managers and located active Windows `winget.exe` and Python 3.12 `pip.exe`.
- **Solution & Resolution:**

---

## [PROBLEM-20261012-02]: Space in User Profile Path Caused MinGW Linker (ld.exe) Failure During CMake Compiler Test
- **Component / Subsystem:** Build System & Linker (`ld.exe` / `CMakeTestCCompiler.cmake`)
- **Symptom & Failure Mode:**
  CMake initial configuration failed with:
  ```
  ld.exe: cannot find C:/Users/SOUNAK: No such file or directory
  ld.exe: cannot find BHATTACHARYA/.../default-manifest.o: No such file or directory
  collect2.exe: error: ld returned 1 exit status
  ```
- **Root Cause Analysis:**
  The portable WinLibs MinGW package was placed by `winget` in `C:\Users\SOUNAK BHATTACHARYA\AppData\Local\...`. GCC's internal specs file passed paths to `default-manifest.o` without escaping/quoting the space between `SOUNAK` and `BHATTACHARYA`, causing `ld.exe` to interpret the single path as two separate file arguments (`C:/Users/SOUNAK` and `BHATTACHARYA/...`).
- **Diagnostic Steps:**
  1. Inspected CMake scratch log `TryCompile-k4t6n6` output.
  2. Isolated the failing `gcc.exe` command line invocation executed by Ninja.
  3. Checked if 8.3 short paths were available (found disabled on volume C:).
- **Solution & Resolution:**
  Relocated the complete MinGW-w64 toolchain to `D:\mingw64` (a clean path without spaces). Configured CMake and Ninja to invoke toolchain binaries from `D:\mingw64\bin`.

---

## [PROBLEM-20261012-03]: Compilation Errors in IntrusiveList Move Constructor, Placement New, and MutexLockGuard Syntax
- **Component / Subsystem:** `kernel/list.hpp`, `kernel/scheduler.cpp`, `kernel/mutex.hpp`
- **Symptom & Failure Mode:**
  1. `list.hpp:38:51`: `error: no matching function for call to 'myrtos::IntrusiveList::RootNode::RootNode()'`
  2. `scheduler.cpp:104:32`: `error: no matching function for call to 'operator new(sizetype, myrtos::TaskControlBlock*&)'`
  3. `mutex.hpp:57:36`: `error: expected ';' at end of member declaration` (`Where_status_is_ok()`)
- **Root Cause Analysis:**
  1. `RootNode` defined an explicit 2-argument constructor, preventing default construction of member `root_` during `IntrusiveList` move construction.
  2. Placement new was invoked without including the freestanding `<new>` header.
  3. Residual syntax typo in `MutexLockGuard::is_locked()`.
- **Diagnostic Steps:**
  Analyzed Ninja C++ compiler diagnostics produced during target builds.
- **Solution & Resolution:**
  1. Initialized `root_(&root_, &root_)` in `IntrusiveList(IntrusiveList&&)`.
  2. Added `#include <new>` in `scheduler.cpp`.
  3. Corrected method signature of `is_locked() const noexcept` in `mutex.hpp`.

---

## [PROBLEM-20261012-04]: Runtime Executable Launch Failure Due to Missing Dynamic MinGW DLLs
- **Component / Subsystem:** Build System & Target Linking (`CMakeLists.txt`, `myrtos_tests`, `myrtos_sim`)
- **Symptom & Failure Mode:**
  Executing `build\myrtos_tests.exe` terminated with exit code 1 and produced empty stdout when invoked from standard terminal environment.
- **Root Cause Analysis:**
  Target executables were dynamically linked by default against MinGW runtime libraries (`libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`). When `D:\mingw64\bin` was not in the system `PATH`, Windows dynamic link loader failed silently to locate the dependency DLLs.
- **Diagnostic Steps:**
  1. Ran executable with `D:\mingw64\bin` explicitly prepended to `PATH`; test executed immediately and passed 7/7 tests.
  2. Verified DLL dependency via compiler toolchain settings.
  Configured static runtime linking (`-static -static-libgcc -static-libstdc++`) for executable targets in `CMakeLists.txt`, generating completely standalone, zero-dependency Windows binaries.

---

## [PROBLEM-20261012-05]: Simulated SysTick Preemption Requires Host Event Synchronization in Idle Task
- **Component / Subsystem:** `port/simulator/port_win32.cpp` & `kernel/src/scheduler.cpp`
- **Symptom & Failure Mode:**
  When tasks called `sleep_ticks()`, the system entered the idle task, but task wakeups did not occur as scheduled because the idle fiber ran continuously without yielding host CPU time slices to the background timer thread.
- **Root Cause Analysis:**
  User-mode Win32 fibers execute on a single host thread context. Asynchronous timer interrupts triggered by a secondary host worker thread cannot forcefully inject PC jumps into a fiber. In the idle state, the fiber must yield host execution time (via `Sleep(1)` or event wait) and dispatch accumulated virtual interrupts.
- **Diagnostic Steps:**
  1. Profiled process CPU consumption during simulation run; observed single host core pegged at 100% by idle fiber loop.
  2. Verified that `Port::yield()` within `idle_task_func` was switching back to itself without host OS synchronization.
  Updated `Port::yield()` to sleep for 1 ms when invoked from the idle task and dispatch any pending interrupts (`dispatch_next_pending()`).

---

## [PROBLEM-20261012-06]: Premature Loop Termination During Delayed Task List Traversal in SysTick ISR
- **Component / Subsystem:** `kernel/src/scheduler.cpp` (`tick_isr()`)
- **Symptom & Failure Mode:**
  Sleeping tasks in `delayed_list_` failed to awaken on their scheduled `wake_tick`.
- **Root Cause Analysis:**
  In `tick_isr()`, the iteration loop tested `curr = (next_node == delayed_list_.head()) ? nullptr : next_node`. Because `delayed_list_.remove(curr)` immediately updated `delayed_list_.head()` to point to `next_node`, the termination condition was triggered prematurely on the very first removal, abandoning inspection of subsequent sleeping tasks.
- **Diagnostic Steps:**
  Traced list pointer updates during removal of first waking task; confirmed that `delayed_list_.head()` matched `next_node` immediately after `remove(curr)`.
- **Solution & Resolution:**
  Refactored delayed list processing to inspect `delayed_list_.head()` in a `while (!delayed_list_.empty())` loop, breaking as soon as `head->wake_tick > current_tick_`.

---

## [PROBLEM-20261012-07]: GLFW 3.4 CMake Configuration Failure in Linux Container Due to Missing wayland-scanner
- **Component / Subsystem:** Docker Container Build & Third-Party UI (`Dockerfile`, `CMakeLists.txt`, `third_party/glfw`)
- **Symptom & Failure Mode:**
  Executing `docker compose up --build` failed at Dockerfile step [12/14] during CMake configuration:
  ```
  CMake Error at build/_deps/glfw-src/src/CMakeLists.txt:76 (message):
    Failed to find wayland-scanner
  -- Configuring incomplete, errors occurred!
  ```
- **Root Cause Analysis:**
  GLFW 3.4 on Linux defaults to building dual display backends (both X11 and Wayland) by having `GLFW_BUILD_WAYLAND` enabled by default. When `GLFW_BUILD_WAYLAND` is active, GLFW's CMake script searches for the `wayland-scanner` binary to generate Wayland protocol client headers. The headless container uses X11 (`Xvfb`, `openbox`, `x11vnc`), not Wayland, so `wayland-scanner` was neither installed nor required.
- **Diagnostic Steps:**
  1. Inspected Docker build logs at step [12/14]; isolated the failing line `build/_deps/glfw-src/src/CMakeLists.txt:76`.
  2. Verified GLFW 3.4 CMake build options; identified `GLFW_BUILD_WAYLAND` (default ON) and `GLFW_BUILD_X11` (default ON).
  3. Confirmed that the headless simulation pipeline relies exclusively on the X11 virtual display (`Xvfb :0`).
- **Solution & Resolution:**
  1. Updated `CMakeLists.txt` to explicitly configure `set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)` and `set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)` before fetching or including GLFW.
  2. Added `-DGLFW_BUILD_WAYLAND=OFF` to the CMake configuration command in `Dockerfile`.
  3. Because `Dockerfile` step [2/14] (`apt-get install`) was kept intact, Docker successfully caches the base layer, allowing subsequent container builds to configure and build in seconds without repeating the 6-minute package download.





