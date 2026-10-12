#pragma once

#include <string>
#include <vector>
#include "kernel/types.hpp"
#include "kernel/scheduler.hpp"

struct GLFWwindow;

namespace myrtos::ui {

struct ExecutionTracePoint {
    TickType tick{0};
    TaskId task_id{INVALID_TASK_ID};
    char task_name[TASK_NAME_MAX_LEN]{};
    Priority priority{0};
};

/**
 * @brief Interactive Real-Time Dashboard & Telemetry Visualizer for myRTOS.
 */
class SimulationDashboard {
public:
    SimulationDashboard() noexcept;
    ~SimulationDashboard() noexcept;

    SimulationDashboard(const SimulationDashboard&) = delete;
    SimulationDashboard& operator=(const SimulationDashboard&) = delete;

    /// Initializes GLFW, OpenGL3, and Dear ImGui context.
    bool init(int width = 1280, int height = 820, const char* title = "myRTOS Interactive Simulation Dashboard") noexcept;

    /// Runs the interactive rendering and simulation loop until window is closed.
    void run() noexcept;

    /// Shuts down UI contexts and destroys window.
    void shutdown() noexcept;

private:
    void render_ui() noexcept;
    void render_task_table() noexcept;
    void render_execution_timeline() noexcept;
    void render_virtual_hardware_panel() noexcept;
    void render_uart_console() noexcept;
    void render_simulation_controls() noexcept;

    GLFWwindow* window_{nullptr};

    // Simulation state
    bool is_running_{true};
    bool is_sim_paused_{false};
    int sim_speed_multiplier_{1};

    // UART Console buffers
    char uart_input_buffer_[256]{};
    std::string uart_terminal_history_{};

    // Execution trace history (rolling buffer)
    static constexpr size_t MAX_TRACE_POINTS = 120;
    std::vector<ExecutionTracePoint> execution_trace_{};
    TickType last_sampled_tick_{0};
};

} // namespace myrtos::ui
