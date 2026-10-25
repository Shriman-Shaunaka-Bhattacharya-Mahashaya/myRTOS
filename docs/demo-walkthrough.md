# Demonstrating myRTOS

The same C++ simulation runs in the native dashboard and the browser. The browser displays the Linux desktop through noVNC. No hardware is required.

## Start

Native build (CMake, Ninja and a C++20 compiler on PATH):

```sh
cmake --preset desktop-debug
cmake --build --preset desktop-debug --parallel 4
ctest --preset desktop-debug
```

Windows: `./out/desktop/myrtos_dashboard.exe`. Linux: `./out/desktop/myrtos_dashboard`.

Browser:

```sh
docker compose up --build -d
```

Open <http://localhost:8080/>. The desktop scales to the browser window. Compose binds the service to this computer's loopback address. Set `MYRTOS_PORT` before starting Compose if port 8080 is occupied.

## A five-minute demonstration

Start with **Teaching mode: Off**. This sequence uses clock Pause and Step; teaching mode has its own controls described below.

1. Point out Producer, Consumer, Heartbeat and Inputs in the task table. Smaller priority numbers run first. UART shows increasing sensor values, and GPIO pin 0 blinks automatically.
2. Click **Pause**. Wait for **Paused** in the header. The tick counter and task activity stop. The dashboard remains responsive.
3. Click **Step 1 tick**. The tick increases by exactly one and ready tasks process that tick. The simulation remains paused. Repeat to demonstrate controlled progress.
4. Click **Reset**. A new run starts paused at tick zero, with the sensor sequence restarted at 100. Initial task work can run at tick zero: the first reading may already be printed and heartbeat pin 0 may already be HIGH. Reset restores 1x speed; it preserves teaching mode and its hold duration.
5. Type `PING` into the UART input and press Enter or Send. While paused, the command waits. Click Step: the console displays `PONG`.
6. Send `LED ON` and step. Pin 1 lights up and the console displays `OK LED ON`. Send `LED OFF` and step to turn it off.
7. Click the LOW button for **Pin 2 (IN)**, then step. Pin 1 follows the input and the console reports `GPIO2 HIGH -> LED ON`. Toggle pin 2 back and step to see the reverse response.
8. Select **0.01x** in the Speed dropdown and resume for slow reading (about ten ticks per second). A default sensor period of 150 ticks then takes about 15 seconds. Try 1x or 10x for faster progress. Speed scales simulated time; host load can affect smoothness.
9. Pause and inspect the scheduler event table. These are committed task selections with tick and source/destination task IDs. In teaching mode, the newest event can describe the target held before its context handoff. Export prepares the displayed recent events as CSV. In the browser, click **Download trace** above the desktop to save it. Native runs save `trace.csv` in the working directory.
10. Click Stop. The UI remains open and marks the session Stopped. Reset starts a fresh paused run. Closing the window ends the application.

## UART commands

Commands are case-insensitive and completed by a newline. The UI adds the newline automatically.

| Command | Result |
| --- | --- |
| `HELP` | Lists accepted commands. |
| `PING` | Replies `PONG`. |
| `STATUS` | Reports tick, sample period, LED state and GPIO2 state. |
| `LED ON` / `LED OFF` | Changes output pin 1 and acknowledges it. |
| `PERIOD 1` through `PERIOD 10000` | Changes the producer's sleep period after its current sleep finishes. |
| Unknown command | Reports an error and suggests HELP. |

Pin 0 is the automatic heartbeat. Pin 1 is the controllable LED. Pin 2 is the input controlling that LED. Other input pins can be toggled for observation, but do not have application actions assigned. When both UART and GPIO are used, the most recently processed action determines pin 1's state.

Commands longer than 127 characters are rejected after the newline. A full UART input buffer rejects the complete new submission and the UI retains its text for retry. With teaching Off, resume or step to process pending input. In Manual teaching mode, advance transitions until the Inputs task executes.

## Explain the implementation

- The UI runs on the main host thread. All simulated tasks share a worker thread.
- Pause is acknowledged at idle, after ready task work settles. A step permits one clock tick plus the work released by that tick.
- GPIO interrupts and UART commands are processed on the worker. Input entered while paused waits for step or resume.
- Reset stops and joins the old worker before replacing its task storage and peripheral state.
- Producer blocks on its bounded queue when needed. Consumer blocks on receiving a reading, avoiding a separate binary signal that could lose backlog notifications.
- The scheduler retains 2,048 switch events; the UI shows the latest 512 and reports older events omitted. Export covers those displayed events, not unlimited history.
- Host stack usage is not measured. Tick samples are observations, not exact CPU-time measurements.

The execution model is cooperative: custom tasks must block, sleep or reach supported runtime boundaries. Forced interruption of an infinite computation and physical-board timing guarantees are outside this desktop demonstration. Stopping abandons suspended C++ task stacks; resources requiring destructors should be owned outside those stacks. The demo follows that ownership rule.

## Reading the task table

Application tasks often complete their work between screen updates, so State now can correctly show Blocked while their switch counts increase. Blocked means waiting for time or data. Dispatch tick records the actual latest dispatch even if the UI did not catch the brief Running state. A green row and Ran recently indicator remain visible for 0.8 seconds after the UI observes another dispatch. Idle is labeled Waiting clock while selected during normal clock waits, or Paused when pause is acknowledged. Stopped tasks show Terminated.

The table describes current scheduler state and recent dispatches [task selections]. It does not promise a minimum one-tick duration for Running. A task can run and block within the same tick.

| Display | Meaning |
| --- | --- |
| Ready | Eligible to run when selected. |
| Running | Selected by the scheduler; normal task work can be very brief. |
| Running (held) | Selected, with live execution held before context handoff by teaching mode. |
| Blocked | Waiting for a deadline, queued data or another event. |
| Waiting clock | Idle is selected while the runtime waits for time or controls. |
| Paused | Idle acknowledged the normal clock pause. |
| Suspended | Explicitly excluded from scheduling. |
| Terminated | Task returned or its session stopped. |
| Dispatch tick | Tick of the most recent scheduler selection; `--` means never selected. |
| Selected | Activity indicator for the current teaching target, before it resumes. |
| Ran recently | The UI observed an increased dispatch count within the last 0.8 seconds. This is an activity hint, not a CPU-time measurement. |

In teaching mode the selected application row stays highlighted throughout its hold. Switches counts selections, not completed task functions. Tick zero can contain several selections.

## Choosing a clock speed

Speed changes the interval between ticks. It does not stretch the instructions inside a task. The UI provides these presets:

| Speed | Target time per tick |
| --- | --- |
| 0.0005x | 2 seconds |
| 0.001x | 1 second |
| 0.005x | 0.2 seconds |
| 0.01x | 0.1 seconds |
| 0.05x | 20 milliseconds |
| 0.1x | 10 milliseconds |
| 0.25x | 4 milliseconds |
| 0.5x | 2 milliseconds |
| 1x | 1 millisecond |
| 2x | 0.5 milliseconds |
| 5x | 0.2 milliseconds |
| 10x | 0.1 milliseconds |

These are target intervals; host load can delay them. At 0.01x, the default 150-tick sensor interval takes about 15 seconds and the 300-tick heartbeat interval takes about 30 seconds. At 0.0005x those intervals take about five and ten minutes respectively. UART commands are normally processed at the next tick, so at 0.0005x a response can wait about two seconds. Pause, Step and Stop do not need to wait for the slow clock interval when teaching is Off.

## Live scheduler teaching mode

Use **Teaching mode** to inspect live task switches. This pauses the simulation itself, so the display does not fall behind execution.

1. Choose **Manual**, then click **Reset** to start at the first task selection.
2. Inputs (ID 4) shows **Running (held)**; the other application tasks are Ready. The selected task has not executed its next instructions yet.
3. Click **Next transition**. Inputs runs, checks commands and sleeps. Producer (ID 1) is now selected and held, while Inputs shows Blocked.
4. Continue to inspect Consumer (ID 2), Heartbeat (ID 3), then Idle (ID 0). Several switches happen at tick zero before the first clock increment.
5. In Manual mode, releasing a hold lets the selected task execute until the next switch. On releasing Idle, the next switch may wait for a clock tick; Next transition is disabled until another hold is reached. Clicks do not queue future transition releases.
6. Choose **Automatic** for timed progression. **Hold seconds** controls the real-world delay at each switch, from 0.1 to 10 seconds; the default is 2 seconds.
7. Choose **Off** to resume normal clock controls. Reset preserves the teaching mode and hold duration. Stop ends a held run immediately.

**Running (held)** means the scheduler has selected that task and execution is held just before the context handoff. The previous task's state and ready/wait lists have already been committed. Pauses occur at these safe dispatch boundaries, including initial startup, rather than midway through list updates or at every individual assignment to a state field. This lets you observe Ready, Running and Blocked in a coherent snapshot.

Clock Pause and Step are disabled while teaching is active; use Next transition in Manual mode. Teaching delay is measured in real seconds and does not add simulation ticks. The clock is rebased after each hold to avoid catching up on the teaching delay. Speed still controls the time between ticks: for a clear first demonstration, keep Speed at 1x and use a 2-second teaching hold. At 0.0005x, a separate two-second clock wait can occur between ticks.

UART and GPIO input can be entered while held. They are processed when the relevant task or interrupt handler is allowed to execute. The UI keeps responding because the worker releases its shared kernel lock before waiting. Teaching mode cannot interrupt a custom task stuck in an infinite computation; it waits for that task to reach a safe switch boundary.

In teaching mode, Dispatch tick and switch counts record scheduler selection before the held target resumes. The Activity cell says Selected for that held target; it does not claim that the target has already executed its task body.

## Recover during rehearsal

- If Pause/Step are disabled, switch Teaching mode to Off. To change teaching mode after Stop, Reset first; this preserves the old mode, then allows selection again.
- If you need to return to the first task, choose Manual and Reset.
- If CMake selects an old compiler, choose the intended toolchain in your terminal before configuring. See [testing.md](testing.md).
- If the browser disconnects, check `docker compose ps` and `docker compose logs --tail 80`. The container has a health check and exits if a supporting display/network process exits.
- Restart with `docker compose restart`. Reconnect the browser afterward.
- Keep the image built before the interview so dependency downloads are not part of the demonstration.
- For browser trace export, click Export inside the desktop, then Download trace in the page header.
- End the browser service with `docker compose down`.

