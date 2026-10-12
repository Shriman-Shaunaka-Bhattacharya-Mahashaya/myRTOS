# myRTOS: Custom Preemptive Real-Time Operating System

[![Standard: C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Build: CMake](https://img.shields.io/badge/Build-CMake%203.20+-064F8C.svg)](https://cmake.org/)
[![Docker: Ready](https://img.shields.io/badge/Docker-Containerized-2496ED.svg)](https://www.docker.com/)
[![UI: Dear ImGui](https://img.shields.io/badge/UI-Dear%20ImGui-orange.svg)](https://github.com/ocornut/imgui)
[![Tests: 7/7 Passing](https://img.shields.io/badge/Tests-7%2F7%20Passed-brightgreen.svg)]()
[![Platform: Windows | Linux | macOS](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)]()
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

Welcome to **myRTOS**! This project is a complete, custom-built, freestanding **RTOS** (*Real-Time Operating System*, an operating system designed to run programs with strictly predictable and guaranteed timing) written in modern C++20 and configured using **CMake** (*a cross-platform tool that automates compiling and linking code files*).

It includes an interactive graphical dashboard built with **Dear ImGui** (*a fast graphical user interface library*) and **GLFW** (*a library for creating desktop windows and handling user inputs*) to let you inspect running tasks, view a live timeline, click virtual buttons, and watch virtual LED lights blink in real time.

It can be run **natively on Windows, Linux, and macOS**, or **completely inside any web browser with zero local installation using Docker and noVNC** (*a web technology that streams the graphical simulator directly to your web browser over WebSockets*)!

---

## 1. Quick Start: Try It in 30 Seconds!

### Option A: Web Browser via Docker (Zero-Install, Recommended)
No need to install compilers, CMake, or graphics drivers!
```bash
# 1. Clone the repository
git clone https://github.com/<your-username>/myRTOS.git
cd myRTOS

# 2. Build and run container
docker compose up --build
```
Now open your web browser to:
👉 **`http://localhost:8080/vnc.html`**
Click **Connect**, and the full interactive RTOS graphical dashboard will appear right in your browser tab!

### Option B: Native Compilation (CMake & Ninja)
If you already have CMake and a C++20 compiler installed:
```bash
cmake -B build -G "Ninja"
cmake --build build

# Launch the interactive graphical simulator
.\build\myrtos_dashboard.exe   # Windows
./build/myrtos_dashboard       # Linux / macOS
```

---

## 2. Key Features & Highlights

- **Freestanding C++20 Kernel Core:** Runs without exceptions (`-fno-exceptions`) or runtime type information (`-fno-rtti`) to guarantee absolute timing predictability and zero dynamic runtime memory allocations.
- **$O(1)$ Bitmap Preemptive Scheduler:** Instantly identifies the highest-priority ready task in constant time using CPU bit-scanning instructions (`count_leading_zeros`). Supports 32 priority levels with Round-Robin time slicing for equal-priority tasks.
- **Priority Inheritance Protocol (PIP):** Prevents Priority Inversion deadlocks (*the famous Mars Pathfinder bug*) by automatically boosting a low-priority task's priority when a high-priority task needs its mutex lock.
- **Rich RTOS Primitives:** Counting and binary semaphores, circular message queues, $O(1)$ fixed-block memory pools, and delta-list software timers.
- **Stack Canary & Memory Watermark Tracking:** Automatically checks for stack memory overflows using a sentinel canary (`0xDEADBEEF`) and tracks the peak memory usage ("high-water mark") of every task.
- **Virtual Hardware Subsystem:** Emulates physical microcontroller electronics, including a 16-channel priority-vectored interrupt controller (VIC), 8 virtual GPIO pins with edge-triggered interrupts, and full-duplex UART serial communication.
- **Dual-Platform Architecture Port:** Runs natively on Windows using **Win32 Fibers** (*isolated user-mode stacks*) and on Linux/macOS/Docker using **POSIX ucontext** (`makecontext`, `swapcontext`).
- **Interactive Dear ImGui Dashboard:** Real-time task telemetry table, live rolling Gantt execution timeline, clickable GPIO switches, animated green LEDs, and bidirectional UART serial console.
- **Zero-Install Web Browser Access:** Packaged with Docker, Xvfb, and noVNC to stream the complete graphical simulator directly to any standard web browser over WebSockets.

---

## Table of Contents
1. [Quick Start: Try It in 30 Seconds!](#1-quick-start-try-it-in-30-seconds)
2. [Key Features & Highlights](#2-key-features--highlights)
3. [What is an RTOS? (Simple Explanation)](#3-what-is-an-rtos-simple-explanation)
4. [Project Architecture: How It Is Structured](#4-project-architecture-how-it-is-structured)
5. [What Tools & Technologies Were Used and Why](#5-what-tools--technologies-were-used-and-why)
6. [How the System Works Internally](#6-how-the-system-works-internally)
   - [Task Control Block & Stacks](#task-control-block-tcb--memory-stacks)
   - [Preemptive Scheduling & Priority Engine](#preemptive-scheduling--priority-engine)
   - [Solving Priority Inversion with Priority Inheritance](#solving-priority-inversion-with-priority-inheritance)
   - [Inter-Task Communication & Synchronization](#inter-task-communication--synchronization)
   - [Deterministic Memory Management](#deterministic-memory-management)
   - [Virtual Hardware & Peripheral Emulation](#virtual-hardware--peripheral-emulation)
   - [Interactive Graphical Dashboard](#interactive-graphical-dashboard)
7. [Project Directory Layout](#7-project-directory-layout)
8. [Docker & Browser-Based Simulation (Zero-Install)](#8-docker--browser-based-simulation-zero-install)
9. [Prerequisites & System Setup (Native Host)](#9-prerequisites--system-setup-native-host)
10. [How to Build the Project (Universal CMake Workflow)](#10-how-to-build-the-project-universal-cmake-workflow)
11. [Running the Executables Locally](#11-running-the-executables-locally)
12. [Documentation & Audit Trail](#12-documentation--audit-trail)

---

## 3. What is an RTOS? (Simple Explanation)

In a regular desktop operating system like Windows or macOS, when you open an application or click a button, you do not mind if it takes 10 milliseconds or 50 milliseconds to react. 

However, in an **embedded system** (*a specialized computer inside a device like a car airbag controller, a medical pacemaker, an aircraft flight computer, or an industrial robot*), timing is critical:
- If a vehicle crashes, the airbag controller **must** trigger the airbag within exactly 5 milliseconds. If it takes 20 milliseconds because the system was busy doing a background update, the passenger is injured.

This strict guarantee of predictable timing is called **Determinism** (*the property where an action always takes the exact same predictable amount of time to complete every single time it runs*).

**myRTOS** provides this exact behavior. It manages multiple independent programs called **Tasks** (*independent functions that run as if they have their own dedicated computer*), scheduling them by **Priority** (*a ranking number from 0 to 31 where lower numbers mean higher importance, so Priority 0 is the most urgent*).

---

## 4. Project Architecture: How It Is Structured

Many developers make the mistake of mixing user interface code directly into operating system code. If you do that, your operating system will only work on a desktop PC and can never run on real microcontroller hardware (*like an STM32, ARM Cortex-M, or ESP32 chip*).

To prevent this, **myRTOS** is strictly partitioned into 4 decoupled layers:

```
+--------------------------------------------------------------------------+
|  Layer 4: Interactive Dashboard & UI Harness (`ui/`)                     |
|  - Graphical User Interface running at 60 FPS (frames per second)        |
|  - Live task status table, stack memory progress bars, rolling timeline  |
|  - Virtual hardware controls (clickable GPIO buttons, virtual UART text) |
+--------------------------------------------------------------------------+
                                    ^
                                    | (Reads telemetry & injects interrupts)
                                    v
+--------------------------------------------------------------------------+
|  Layer 3: Virtual Hardware & Peripheral Simulation (`hw/`)               |
|  - Virtual Interrupt Controller: Manages 16 hardware interrupt lines     |
|  - Virtual SysTick: Periodic 1 millisecond clock heartbeat               |
|  - Virtual GPIO: 8 digital input/output pins with edge-triggered signals |
|  - Virtual UART: Serial communication port with circular text buffers    |
+--------------------------------------------------------------------------+
                                    ^
                                    |
                                    v
+--------------------------------------------------------------------------+
|  Layer 2: Architecture Port Layer (`port/`)                              |
|  - Windows Port: Win32 Fibers (`port_win32.cpp`) for manual task stacks  |
|  - POSIX Port: Linux/Docker ucontext (`port_posix.cpp`) for Unix stacks  |
|  - Critical Sections: Locks that temporarily mask interrupts             |
|  - Host Timer Thread: Drives the simulated 1 millisecond heartbeat       |
+--------------------------------------------------------------------------+
                                    ^
                                    |
                                    v
+--------------------------------------------------------------------------+
|  Layer 1: RTOS Kernel Core (`kernel/`) [Freestanding C++20]              |
|  - O(1) Bitmap Preemptive Scheduler (picks highest priority instantly)   |
|  - Task Control Blocks (tracks state, priority, and stack limits)        |
|  - Mutex with Priority Inheritance Protocol (prevents lock deadlocks)    |
|  - Semaphores, Message Queues, Memory Pools, and Software Timers         |
+--------------------------------------------------------------------------+
```

1. **Layer 1: Kernel Core (`kernel/`)**: Pure **freestanding** (*code that runs on raw memory without needing Windows, Linux, or standard desktop libraries*) C++20. It has zero knowledge of windows, screens, or graphics.
2. **Layer 2: Port Layer (`port/`)**: The bridge between the kernel and the CPU.
   - On Windows, it uses **Win32 Fibers** (*a Windows feature that allows manual switching between isolated memory stacks*).
   - On Linux and Docker containers, it uses **POSIX ucontext** (*the standard UNIX/Linux operating system interface for creating, swapping, and managing memory stacks*).
   - On physical silicon, this layer would be replaced by a small assembly file handling CPU interrupt registers for ARM Cortex-M or RISC-V.
3. **Layer 3: Virtual Hardware (`hw/`)**: Emulates physical electronics, including **GPIO** (*General-Purpose Input/Output pins, physical electrical pins used to read buttons or power LEDs*) and **UART** (*Universal Asynchronous Receiver-Transmitter, a serial wire protocol used to send text characters*).
4. **Layer 4: UI Dashboard (`ui/`)**: A dedicated desktop application that samples the kernel from the outside and displays real-time telemetry visually.

---

## 5. What Tools & Technologies Were Used and Why

| Technology | What It Is | Why It Was Chosen |
| :--- | :--- | :--- |
| **C++20** | Modern programming language | Allows strong type safety, static checks, and zero-cost abstractions (*clean code that compiles down to the fastest possible machine code without extra runtime penalty*). Compiled with `-fno-exceptions` (*disables C++ exceptions to guarantee predictable timing*) and `-fno-rtti` (*disables runtime type information to save memory*). |
| **CMake (version 3.20+)** | Cross-platform build generator | Standard across the software engineering industry. Automatically generates native build files for any computer, compiler, or IDE. Uses `FetchContent` (*CMake feature that downloads missing libraries automatically during setup*) for third-party dependencies. |
| **MinGW-w64 / GCC / Clang / MSVC** | C++ Compiler toolchain | Translates C++ code into native machine instructions. On Windows, MinGW-w64 provides a fast, open-source GCC environment with 64-bit UCRT (*Universal C Runtime*) support. |
| **Ninja / Make** | High-speed build executor | Runs the actual compilation commands in parallel across multiple CPU cores to build the project in seconds. |
| **Win32 Fibers** | Windows operating system API feature | Fibers give each RTOS task its own isolated block of memory for its stack on Windows. When the RTOS switches tasks, it swaps the CPU's execution point from one fiber to another, exactly like real hardware registers (*CPU internal storage slots*) on a microcontroller. |
| **POSIX ucontext & pthreads** | Standard Linux context switching and thread API | Enables myRTOS to run identically on Linux, macOS, and inside Docker containers using native operating system context primitives (`makecontext`, `swapcontext`). |
| **Docker & Docker Compose** | Software containerization platform | Packages the entire operating system, compiler toolchain, and graphical display server into an isolated container (*a lightweight software package containing everything needed to run*) so anyone can run the project with a single command without installing compilers or libraries on their host machine. |
| **Xvfb & Openbox** | Headless virtual display and window manager | **Xvfb** (*X Virtual Framebuffer, an in-memory virtual screen that renders graphics without needing a physical computer monitor*) creates an invisible 1280x820 display, and **Openbox** (*a lightweight window manager*) organizes the simulation window on that virtual screen. |
| **x11vnc & noVNC (WebSockets)** | Remote screen capture and web browser streaming | **x11vnc** (*a VNC screen sharing server*) captures the virtual screen in memory, and **noVNC** translates the screen frames into **WebSockets** (*a continuous two-way communication channel between browser and server*) so you can control the GUI inside your web browser. |
| **Dear ImGui** | Immediate-mode graphical UI library | The industry standard for embedded simulators, robotics tools, and game engines. It is lightweight, renders at 60 FPS, and requires zero background services. |
| **GLFW** | OpenGL window and input manager | Handles creating the desktop window, OpenGL graphics context (*the connection to your graphics card*), and keyboard/mouse events. |

---

## 6. How the System Works Internally

### Task Control Block (TCB) & Memory Stacks
Every task in **myRTOS** has a **TCB** (*Task Control Block, an internal tracking record storing everything about a task*):
- **Task ID**: Unique identification number.
- **Task Name**: Up to 16 characters (*e.g., "Producer", "Heartbeat"*).
- **Task State**: 
  - `READY`: Ready to run as soon as CPU time is available.
  - `RUNNING`: Currently executing instructions on the CPU.
  - `BLOCKED`: Waiting for a timer to expire, a semaphore signal, or a mutex lock.
  - `SUSPENDED`: Manually paused by the user or another task.
- **Priority**: Number from 0 (*most urgent*) to 31 (*least urgent / Idle*).
- **Stack Memory**: A dedicated memory area where the task keeps its local variables.
- **Stack Canary**: A special number (`0xDEADBEEF`) written at the very bottom boundary of the task's stack memory. If a task uses too much memory and overflows its buffer, it overwrites this number. The kernel immediately detects this and raises an error, preventing memory corruption.
- **High-Water Mark**: The maximum peak memory used by the task. The system checks this continuously to tell you how much safe stack headroom remains.

---

### Preemptive Scheduling & Priority Engine
How does the RTOS decide who runs?
1. The scheduler maintains 32 **Ready Queues** (*lists of tasks that are waiting to run, one list per priority level*).
2. It uses a 32-bit number called a **Priority Bitmap** (*a single number where each bit 0-31 is turned on (1) if that priority has tasks ready to run, or off (0) if empty*).
3. Finding the highest-priority task is done in **$O(1)$ time** (*instant constant time that takes the exact same number of nanoseconds whether there is 1 task or 1,000 tasks*). It uses a CPU hardware instruction called `count_leading_zeros` (*an instruction that counts leading zero bits to identify the highest set bit instantly*).
4. **Preemption** (*the act of stopping a lower-priority task immediately when a higher-priority task wakes up*):
   - If Task $A$ (Priority 5) is running, and Task $B$ (Priority 2) wakes up from a timer sleep, the scheduler immediately pauses Task $A$, saves its position, and switches execution to Task $B$.
5. **Round-Robin** (*sharing time equally*): If two tasks have the exact same priority (*e.g., both are Priority 3*), the scheduler lets them take turns every time the timer ticks.

---

### Solving Priority Inversion with Priority Inheritance
**Priority Inversion** (*a critical bug that famously caused NASA's Mars Pathfinder spacecraft to repeatedly reboot in 1997*) happens like this:
1. Low-priority Task $L$ acquires a **Mutex** (*Mutual Exclusion lock, a digital lock on a shared resource like a sensor or printer*).
2. High-priority Task $H$ wakes up and wants to use the sensor, so it tries to acquire the Mutex and **blocks** (*pauses until the lock is released*).
3. An unrelated Medium-priority Task $M$ wakes up. Because $M$ has higher priority than $L$, the scheduler pauses $L$ and lets $M$ run.
4. **The Disaster:** High-priority Task $H$ is now stuck waiting for Low-priority Task $L$, but $L$ cannot finish because Medium-priority Task $M$ is hogging the CPU! $M$ is effectively delaying $H$, even though $H$ has higher priority than $M$!

**How myRTOS solves it:**
We implemented the **Priority Inheritance Protocol (PIP)**:
- The instant High-priority Task $H$ tries to acquire a Mutex owned by Low-priority Task $L$, the kernel automatically **boosts** $L$'s priority up to match $H$'s priority!
- Now, Medium Task $M$ **cannot** interrupt $L$.
- $L$ runs without interruption, finishes its work, and unlocks the Mutex.
- As soon as $L$ unlocks the Mutex, its priority drops back down to normal, and High-priority Task $H$ instantly grabs the lock and executes!

---

### Inter-Task Communication & Synchronization
Tasks often need to send data to each other or signal each other safely:
1. **Semaphores** (*digital signaling counters*):
   - A task can wait for a semaphore token (`acquire()`). If no token is available, the task goes to sleep.
   - Another task or a hardware interrupt can give a token (`release()`), which immediately wakes up the highest-priority waiting task.
2. **Message Queues** (*safe data mailboxes*):
   - A ring buffer (*circular memory queue*) that stores messages copy-by-value (*copies data directly so memory isn't shared dangerously*).
   - If the queue is empty, reading tasks sleep until data arrives. If the queue is full, writing tasks sleep until space opens up.
   - Requires zero dynamic heap allocation (*no slow `malloc` or `new` calls*).

---

### Deterministic Memory Management
In embedded real-time code, using standard C++ `new` or C `malloc` is strictly forbidden because of **Memory Fragmentation** (*the problem where free memory gets broken up into tiny scattered holes over time, causing allocations to fail even if plenty of total memory is free*).

**myRTOS** uses an **$O(1)$ Fixed-Block Memory Pool**:
- At startup, a chunk of memory is divided into identical blocks (*for example, 64-byte blocks*).
- Each unused block points directly to the next free block.
- Allocating a block takes exactly 3 CPU instructions (*instant*).
- Freeing a block takes exactly 3 CPU instructions (*instant*).
- Zero fragmentation is mathematically guaranteed.

---

### Virtual Hardware & Peripheral Emulation
To make this RTOS feel like a real physical microchip on your PC:
- **SysTick Timer**: A background thread generates a timer tick interrupt every 1 millisecond (1000 times per second), acting like the heartbeat of the microchip.
- **Virtual Interrupt Controller (VIC)**: Simulates a hardware interrupt chip managing 16 interrupt lines. When an interrupt occurs, it interrupts whatever is running and calls the corresponding **ISR** (*Interrupt Service Routine, an urgent function registered to respond to an event*).
- **Virtual GPIO**: 8 digital pins (Pin 0 to Pin 7). You can configure them as inputs or outputs, and set edge triggers (*trigger an interrupt when a pin changes from LOW to HIGH*).
- **Virtual UART**: A serial communications controller with independent send and receive buffers, just like plugging a USB-to-Serial cable into an Arduino or STM32 board.

---

### Interactive Graphical Dashboard
When you launch `myrtos_dashboard.exe`, a window opens up showing:
1. **Task Monitor Table**: Shows all active tasks, their current state (*RUNNING, READY, BLOCKED*), priority, stack watermark, and whether Priority Inheritance has boosted their priority.
2. **Execution Timeline (Gantt Chart)**: A rolling colorful timeline that visualizes exactly which task was using the CPU on every single tick.
3. **Virtual Hardware Control Panel**:
   - 8 Clickable GPIO buttons to simulate user button presses or external sensor switches.
   - 8 Animated Virtual LEDs that light up green when a pin goes HIGH.
4. **Virtual UART Serial Console**:
   - Displays live text sent by tasks over the virtual serial port.
   - A text box where you can type commands and send them to the RTOS as simulated incoming serial data.
5. **Simulation Controls**: Buttons to Pause, Resume, or Step the operating system 1 single tick at a time.

---

## 7. Project Directory Layout

```
myRTOS/
├── CMakeLists.txt              # Root build script with cross-platform and FetchContent support
├── Dockerfile                  # Container definition (Ubuntu 24.04, C++20, OpenGL, noVNC)
├── docker-compose.yml          # Single-command container launcher mapping web port 8080
├── entrypoint.sh               # Headless Xvfb + x11vnc + noVNC browser streaming launcher
├── README.md                   # Complete beginner-friendly documentation (this file)
├── changes.md                  # Strict append-only log of all features & architectural changes
├── problems.md                 # Strict append-only log of all bugs encountered & diagnosed
├── .gitignore                  # Git ignore rules for build artifacts and temporary files
│
├── kernel/                     # Pure freestanding RTOS core
│   ├── include/kernel/
│   │   ├── types.hpp           # Basic types, states, priorities, status codes, and bit intrinsics
│   │   ├── list.hpp            # Intrusive doubly-linked circular list
│   │   ├── tcb.hpp             # Task Control Block, stack canary, and watermark calculations
│   │   ├── scheduler.hpp       # O(1) bitmap preemptive scheduler header
│   │   ├── semaphore.hpp       # Counting and binary semaphore headers
│   │   ├── mutex.hpp           # Mutex with Priority Inheritance Protocol (PIP)
│   │   ├── queue.hpp           # Circular ring-buffer message queue
│   │   ├── memory_pool.hpp     # O(1) fixed-block pool allocator
│   │   └── timer.hpp           # Delta-list software timers
│   └── src/
│       ├── scheduler.cpp       # Scheduler implementation & idle task loop
│       ├── semaphore.cpp       # Semaphore acquire/release logic
│       ├── mutex.cpp           # Mutex locking, PIP elevation & restoration
│       ├── queue.cpp           # Message queue enqueue/dequeue logic
│       ├── memory_pool.cpp     # Memory pool free-list chaining logic
│       └── timer.cpp           # Software timer processing logic
│
├── port/                       # Hardware abstraction / Porting layer
│   ├── include/port/
│   │   └── port.hpp            # Port interface & RAII critical section guard
│   └── simulator/
│       ├── port_win32.cpp      # Windows: Win32 Fibers, critical sections, and 1ms timer
│       └── port_posix.cpp      # Linux/Docker: POSIX ucontext, pthreads, and 1ms timer
│
├── hw/                         # Virtual hardware peripherals
│   ├── include/hw/
│   │   ├── vic.hpp             # Virtual Interrupt Controller interface
│   │   ├── gpio.hpp            # Virtual GPIO interface
│   │   └── uart.hpp            # Virtual UART interface
│   └── src/
│       ├── vic.cpp             # Interrupt priority dispatch logic
│       ├── gpio.cpp            # Pin state & edge trigger interrupt generation
│       └── uart.cpp            # Serial circular buffers & simulated RX/TX
│
├── ui/                         # Dear ImGui telemetry & observability dashboard
│   ├── include/ui/
│   │   └── dashboard.hpp       # Dashboard window class & telemetry models
│   └── src/
│       └── dashboard.cpp       # Dear ImGui rendering, Gantt timeline, and widgets
│
├── tests/
│   └── main_tests.cpp          # Automated unit test suite verifying all 7 core subsystems
│
└── examples/
    ├── main_sim.cpp            # Command-line multi-task simulation demonstration
    └── main_gui.cpp            # Graphical simulation application entry point (portable std::thread)
```

---

## 8. Docker & Browser-Based Simulation (Zero-Install)

If you do not want to install C++ compilers, MinGW, CMake, or graphics drivers onto your computer, you can run **myRTOS** inside a **Docker container** (*an isolated, portable software environment that has everything pre-configured*) and view the full interactive Dear ImGui simulation dashboard right inside your favorite **web browser** (*like Chrome, Edge, Firefox, or Safari*)!

### How Does Browser Simulation Work?
When you start the container, it creates an automated 4-tier streaming pipeline:

```
+--------------------------------------------------------------------------------------------------+
| Inside Docker Container (Ubuntu 24.04)                                                           |
|                                                                                                  |
| [myRTOS + Dear ImGui]                                                                            |
|          | (Renders 60 FPS graphics)                                                             |
|          v                                                                                       |
|   [Xvfb Server]          -> Invisible virtual screen in memory (:0, 1280x820)                    |
|          |                                                                                       |
|          v                                                                                       |
|   [x11vnc Server]        -> Captures virtual screen frames on internal port 5900                 |
|          |                                                                                       |
|          v                                                                                       |
|   [noVNC / Websockify]   -> Converts video frames to WebSockets on HTTP port 8080                |
+--------------------------------------------------------------------------------------------------+
                                           |
                                           v (Streams over your local network)
+--------------------------------------------------------------------------------------------------+
| Your Host PC Web Browser                                                                         |
|                                                                                                  |
|   http://localhost:8080/vnc.html                                                                 |
|   - Live Task Status Table & Memory Gauges                                                       |
|   - Live Gantt Execution Timeline                                                                |
|   - Clickable GPIO Buttons & Animated Green LEDs                                                 |
|   - Interactive UART Serial Terminal                                                             |
+--------------------------------------------------------------------------------------------------+
```

1. **Xvfb** (*X Virtual Framebuffer*): Creates a virtual computer monitor directly inside computer RAM memory without requiring a physical screen or graphics card.
2. **Openbox**: A lightweight **Window Manager** (*software that arranges windows on screen*) that gives the simulation window focus.
3. **x11vnc**: A screen capture server that reads the virtual memory display and prepares it for streaming.
4. **noVNC & Websockify**: An HTML5 web application that translates the screen stream into **WebSockets** (*a real-time bidirectional web protocol*) so you can view and click buttons in the simulator through any web browser.

---

### Step-by-Step Instructions: Running in Docker

#### 1. Start the Container
From your terminal in the `myRTOS` directory, run:
```bash
docker compose up --build
```
*Docker will download Ubuntu 24.04, compile the entire RTOS kernel and GUI with Ninja, verify all 7 unit tests, and launch the web streaming server.*

#### 2. Open Your Web Browser
Open your browser and navigate to:
```
http://localhost:8080/vnc.html
```

#### 3. Connect to the Simulator
Click the blue **Connect** button on the screen. The full **Dear ImGui myRTOS Dashboard** will instantly appear in your browser tab! You can:
- Click the **Pin 2 (Button)** switch to trigger hardware interrupts.
- Watch the **Heartbeat LED** pulse in real-time.
- Watch tasks execute on the **Rolling Gantt Timeline**.
- Type messages in the **UART Serial Console** and receive real-time replies.
- Pause or single-step ticks with the **Simulation Control Bar**.

#### 4. Stopping the Container
When you are done, press `Ctrl + C` in your terminal, or run:
```bash
docker compose down
```

#### 5. Running Tests or CLI Demo Inside Docker
You can also run the automated tests or terminal demo inside Docker:
```bash
# Run automated kernel verification tests
docker run --rm -it myrtos-simulator /app/build/myrtos_tests

# Run terminal CLI multi-tasking simulation
docker run --rm -it myrtos-simulator /app/build/myrtos_sim
```

---

## 9. Prerequisites & System Setup (Native Host)

If you prefer compiling and running natively on your computer without Docker, you will need:

1. **Git** (*to clone the repository*): [https://git-scm.com/](https://git-scm.com/)
2. **CMake (version 3.20 or newer)**: [https://cmake.org/download/](https://cmake.org/download/)
3. **A C++20-compliant Compiler**:
   - **Windows**: **MinGW-w64** (GCC 11+, 14+, or 16+), **LLVM Clang**, or **Microsoft Visual Studio 2019+**.
   - **Linux**: GCC 11+ (`sudo apt install build-essential cmake ninja-build libgl1-mesa-dev xorg-dev`).
   - **macOS**: Apple Clang or Homebrew GCC (`brew install cmake ninja`).
   - **Quick Install on Windows (PowerShell)**:
     ```powershell
     winget install Kitware.CMake
     winget install BrechtSanders.WinLibs.POSIX.UCRT
     winget install Ninja-build.Ninja
     ```

> [!NOTE]
> **Automatic Dependency Fetching:** You do **not** need to manually download or install Dear ImGui or GLFW. CMake's `FetchContent` will automatically download them from GitHub during the first build if they are not already present in `third_party/`.

---

## 10. How to Build the Project (Universal CMake Workflow)

Any user who clones this repository from GitHub can build the project using standard, portable commands without hardcoded paths:

### Step 1: Clone the Repository
```bash
git clone https://github.com/<your-username>/myRTOS.git
cd myRTOS
```

### Step 2: Configure the Build with CMake
Run CMake to generate the build files into a folder named `build`:

- **Using Ninja (Fastest):**
  ```bash
  cmake -B build -G "Ninja"
  ```
- **Or using MinGW Makefiles (Windows):**
  ```bash
  cmake -B build -G "MinGW Makefiles"
  ```
- **Or using the default system compiler (e.g., Linux Makefiles or Visual Studio):**
  ```bash
  cmake -B build
  ```

### Step 3: Compile All Targets
Tell CMake to compile the project:
```bash
cmake --build build
```

This compiles:
1. `myrtos_kernel` (Static library containing the freestanding core RTOS)
2. `myrtos_port` (Static library containing Win32 fiber or Linux POSIX ucontext port)
3. `myrtos_hw` (Static library containing virtual peripherals)
4. `myrtos_ui` (Static library containing Dear ImGui dashboard)
5. `myrtos_tests` (Automated verification test suite executable)
6. `myrtos_sim` (Command-line multi-tasking simulation executable)
7. `myrtos_dashboard` (Full interactive graphical user interface executable)

---

## 11. Running the Executables Locally

Once built, all executables are placed in the `build/` directory:

### 1. Run the Automated Kernel Test Suite
Verifies all 7 core subsystems (lists, stack canaries, priority bitmap, memory pool, message queue, interrupt controller, and UART):

- **Windows:**
  ```powershell
  .\build\myrtos_tests.exe
  ```
- **Linux / macOS:**
  ```bash
  ./build/myrtos_tests
  ```

**Expected Output:**
```
========================================
       myRTOS Kernel Verification       
========================================
[TEST] 1. Intrusive List Operations... PASSED
[TEST] 2. Stack Canary & Watermark Tracking... PASSED
[TEST] 3. O(1) Bitmap Priority Resolution... PASSED
[TEST] 4. O(1) Memory Pool Allocator... PASSED
[TEST] 5. Circular Message Queue... PASSED
[TEST] 6. Virtual Interrupt Controller & Virtual GPIO... PASSED
[TEST] 7. Virtual UART Loopback & Buffers... PASSED
========================================
 ALL DETERMINISTIC TESTS PASSED (7/7)   
========================================
```

---

### 2. Run the Terminal Multi-Tasking Simulation Demo
Runs an interactive CLI demonstration with a Producer task, Consumer task, and Heartbeat task printing real-time telemetry tables to your console:

- **Windows:**
  ```powershell
  .\build\myrtos_sim.exe
  ```
- **Linux / macOS:**
  ```bash
  ./build/myrtos_sim
  ```
*(Press `Ctrl + C` in your terminal when you want to stop it).*

---

### 3. Launch the Graphical Simulation Dashboard
Opens the Dear ImGui desktop window with the live Gantt timeline, interactive GPIO switches, LED lights, and serial console:

- **Windows:**
  ```powershell
  .\build\myrtos_dashboard.exe
  ```
- **Linux / macOS:**
  ```bash
  ./build/myrtos_dashboard
  ```

Inside the graphical dashboard:
- **Task Table**: Inspect each task's state (`RUNNING`, `READY`, `BLOCKED`), stack free bytes, and stack utilization progress bars.
- **Live Gantt Timeline**: Watch colored blocks representing task execution slices in real time.
- **Clickable GPIO**: Click the **HIGH / LOW** button on Pin 2 to inject simulated button presses and trigger hardware interrupts.
- **Virtual LEDs**: Watch the green LED for Pin 0 flash automatically as the Heartbeat task toggles it.
- **Serial Console**: Read live sensor data transmitted over virtual UART, or type text into the input field and click **Send** to send serial data to the RTOS.
- **Simulation Control**: Click **Pause Simulation** to stop time, or **Step Single Tick (1 ms)** to step execution one millisecond at a time!

---

## 12. Documentation & Audit Trail

In accordance with strict systems engineering practices, this repository maintains two persistent historical logs:

1. **[changes.md](changes.md)**:
   - Contains the approved architectural specification as its foundation.
   - An append-only historical log recording every single feature added, modified, or refactored, along with the technical rationale behind each change.
2. **[problems.md](problems.md)**:
   - An append-only diagnostic log documenting every bug, compiler defect, link error, or concurrency issue encountered during development.
   - Each entry details the symptom, root cause, diagnostic steps, and the permanent engineering resolution applied.

