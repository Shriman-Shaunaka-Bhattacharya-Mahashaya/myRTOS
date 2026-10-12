#include "ui/dashboard.hpp"
#include "hw/gpio.hpp"
#include "hw/uart.hpp"
#include "hw/vic.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <cstdio>
#include <cstring>

namespace myrtos::ui {

SimulationDashboard::SimulationDashboard() noexcept {
    uart_input_buffer_[0] = '\0';
    execution_trace_.reserve(MAX_TRACE_POINTS);
}

SimulationDashboard::~SimulationDashboard() noexcept {
    shutdown();
}

bool SimulationDashboard::init(int width, int height, const char* title) noexcept {
    if (!glfwInit()) {
        return false;
    }

    // GL 3.3 + GLSL 130
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Apply sleek modern dark styling
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    return true;
}

void SimulationDashboard::run() noexcept {
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();

        // Sample virtual UART stream into terminal history
        char uart_buf[256];
        size_t pulled = hw::VirtualUart::instance().pull_transmitted_data(uart_buf, sizeof(uart_buf) - 1);
        if (pulled > 0) {
            uart_buf[pulled] = '\0';
            uart_terminal_history_.append(uart_buf);
            // Cap terminal size to last 4000 characters
            if (uart_terminal_history_.size() > 4000) {
                uart_terminal_history_.erase(0, uart_terminal_history_.size() - 4000);
            }
        }

        // Sample active running task for execution trace timeline
        TickType current_tick = Scheduler::instance().tick_count();
        if (current_tick != last_sampled_tick_) {
            last_sampled_tick_ = current_tick;
            TaskControlBlock* current = Scheduler::instance().current_task();
            if (current != nullptr) {
                ExecutionTracePoint pt{};
                pt.tick = current_tick;
                pt.task_id = current->id;
                std::strncpy(pt.task_name, current->name, TASK_NAME_MAX_LEN - 1);
                pt.priority = current->current_priority;

                if (execution_trace_.size() >= MAX_TRACE_POINTS) {
                    execution_trace_.erase(execution_trace_.begin());
                }
                execution_trace_.push_back(pt);
            }
        }

        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        render_ui();

        // Rendering
        ImGui::Render();
        int display_w = 0, display_h = 0;
        glfwGetFramebufferSize(window_, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.12f, 0.13f, 0.16f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window_);
    }
}

void SimulationDashboard::render_ui() noexcept {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("DashboardRoot", nullptr, window_flags);

    render_simulation_controls();
    ImGui::Separator();

    // Two-column layout: Left for Tasks & Timeline, Right for Virtual Peripherals
    ImGui::Columns(2, "MainLayout", true);
    ImGui::SetColumnWidth(0, viewport->WorkSize.x * 0.58f);

    render_task_table();
    ImGui::Spacing();
    render_execution_timeline();

    ImGui::NextColumn();

    render_virtual_hardware_panel();
    ImGui::Spacing();
    render_uart_console();

    ImGui::Columns(1);
    ImGui::End();
}

void SimulationDashboard::render_simulation_controls() noexcept {
    ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "myRTOS INTERACTIVE TELEMETRY & CONTROL DASHBOARD");
    ImGui::SameLine();
    ImGui::Text("| System Tick: %u ms | Active Tasks: %zu", 
                Scheduler::instance().tick_count(), Scheduler::instance().task_count());

    ImGui::Spacing();

    if (ImGui::Button(is_sim_paused_ ? "Resume Simulation" : "Pause Simulation", ImVec2(140, 26))) {
        is_sim_paused_ = !is_sim_paused_;
    }

    ImGui::SameLine();
    if (ImGui::Button("Step Single Tick (1 ms)", ImVec2(160, 26))) {
        hw::VirtualInterruptController::instance().trigger_irq(hw::IRQ_SYSTICK);
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    ImGui::SliderInt("Speed", &sim_speed_multiplier_, 1, 10, "%dx");
}

void SimulationDashboard::render_task_table() noexcept {
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Task Control Blocks & Execution State");

    TaskMetrics metrics[MAX_TASKS];
    size_t count = Scheduler::instance().get_task_metrics(metrics, MAX_TASKS);

    static ImGuiTableFlags flags = ImGuiTableFlags_Borders | 
                                   ImGuiTableFlags_RowBg | 
                                   ImGuiTableFlags_SizingStretchProp;

    if (ImGui::BeginTable("TaskTable", 7, flags)) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 35.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableSetupColumn("Priority", ImGuiTableColumnFlags_WidthFixed, 65.0f);
        ImGui::TableSetupColumn("Stack Free", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Stack Usage", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Switches", ImGuiTableColumnFlags_WidthFixed, 65.0f);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < count; ++i) {
            ImGui::TableNextRow();

            // ID
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%u", metrics[i].id);

            // Name
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(metrics[i].name);

            // State (Color coded)
            ImGui::TableSetColumnIndex(2);
            switch (metrics[i].state) {
                case TaskState::Running:
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "RUNNING");
                    break;
                case TaskState::Ready:
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "READY");
                    break;
                case TaskState::Blocked:
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "BLOCKED");
                    break;
                case TaskState::Suspended:
                    ImGui::TextColored(ImVec4(0.8f, 0.4f, 1.0f, 1.0f), "SUSPEND");
                    break;
                default:
                    ImGui::Text("TERM");
                    break;
            }

            // Priority (Highlight if PIP elevation active)
            ImGui::TableSetColumnIndex(3);
            if (metrics[i].current_priority < metrics[i].base_priority) {
                ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), "%u (PIP!)", metrics[i].current_priority);
            } else {
                ImGui::Text("%u", metrics[i].current_priority);
            }

            // Stack Free
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%zu B", metrics[i].stack_free_bytes);

            // Stack Usage Progress Bar
            ImGui::TableSetColumnIndex(5);
            char prog_str[32];
            std::snprintf(prog_str, sizeof(prog_str), "%.1f%%", metrics[i].stack_usage_percent);
            float fraction = metrics[i].stack_usage_percent / 100.0f;
            ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 16.0f), prog_str);

            // Context Switches
            ImGui::TableSetColumnIndex(6);
            ImGui::Text("%u", metrics[i].context_switch_count);
        }

        ImGui::EndTable();
    }
}

void SimulationDashboard::render_execution_timeline() noexcept {
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Live Execution Timeline (Gantt Rolling Window)");

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float avail_width = ImGui::GetContentRegionAvail().x;
    float height = 50.0f;

    // Background track
    draw_list->AddRectFilled(p0, ImVec2(p0.x + avail_width, p0.y + height), IM_COL32(30, 32, 40, 255), 4.0f);
    draw_list->AddRect(p0, ImVec2(p0.x + avail_width, p0.y + height), IM_COL32(60, 65, 80, 255), 4.0f);

    if (!execution_trace_.empty()) {
        float block_width = avail_width / static_cast<float>(MAX_TRACE_POINTS);
        for (size_t i = 0; i < execution_trace_.size(); ++i) {
            float x1 = p0.x + (i * block_width);
            float x2 = x1 + block_width;

            // Generate deterministic color from task ID
            ImU32 col = IM_COL32(80, 80, 80, 255);
            switch (execution_trace_[i].task_id) {
                case 0: col = IM_COL32(50, 55, 65, 255); break; // IDLE
                case 1: col = IM_COL32(0, 180, 180, 255); break; // Producer
                case 2: col = IM_COL32(180, 80, 220, 255); break; // Consumer
                case 3: col = IM_COL32(50, 200, 100, 255); break; // Heartbeat
                default: col = IM_COL32(220, 120, 50, 255); break;
            }

            draw_list->AddRectFilled(ImVec2(x1, p0.y + 4.0f), ImVec2(x2, p0.y + height - 4.0f), col);
        }
    }

    ImGui::Dummy(ImVec2(avail_width, height));
    ImGui::TextDisabled("Rolling buffer: Right-most is latest running task.");
}

void SimulationDashboard::render_virtual_hardware_panel() noexcept {
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Virtual Hardware Peripherals (GPIO & Interrupts)");

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // 8 Virtual GPIO Pins
    for (uint8_t pin = 0; pin < hw::NUM_GPIO_PINS; ++pin) {
        const auto& info = hw::VirtualGpio::instance().get_pin_info(pin);

        ImGui::PushID(pin);
        char pin_label[32];
        std::snprintf(pin_label, sizeof(pin_label), "Pin %u (%s)", pin, 
                      info.direction == hw::PinDirection::Input ? "IN" : "OUT");

        bool pin_is_high = (info.state == hw::PinState::High);

        // Clickable button for inputs
        if (info.direction == hw::PinDirection::Input) {
            if (ImGui::Button(pin_is_high ? "HIGH" : "LOW", ImVec2(55, 24))) {
                hw::VirtualGpio::instance().simulate_input_change(
                    pin, pin_is_high ? hw::PinState::Low : hw::PinState::High
                );
            }
        } else {
            ImGui::Text("%s", pin_is_high ? "HIGH" : "LOW");
        }

        ImGui::SameLine();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImU32 led_color = pin_is_high ? IM_COL32(50, 255, 50, 255) : IM_COL32(60, 60, 60, 255);
        draw_list->AddCircleFilled(ImVec2(p.x + 10, p.y + 12), 8.0f, led_color);
        if (pin_is_high) {
            draw_list->AddCircle(ImVec2(p.x + 10, p.y + 12), 10.0f, IM_COL32(100, 255, 100, 150), 0, 2.0f);
        }

        ImGui::Dummy(ImVec2(24, 24));
        ImGui::SameLine();
        ImGui::Text("%s", pin_label);

        ImGui::PopID();
    }
}

void SimulationDashboard::render_uart_console() noexcept {
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Virtual UART Console");

    // Output Box
    ImGui::BeginChild("UartScrolling", ImVec2(-FLT_MIN, 160.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(uart_terminal_history_.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();

    // Input Bar
    ImGui::PushItemWidth(-70.0f);
    bool submit = ImGui::InputText("##UartInput", uart_input_buffer_, sizeof(uart_input_buffer_), 
                                  ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopItemWidth();

    ImGui::SameLine();
    if (ImGui::Button("Send", ImVec2(60, 0)) || submit) {
        if (uart_input_buffer_[0] != '\0') {
            size_t len = std::strlen(uart_input_buffer_);
            hw::VirtualUart::instance().simulate_incoming_data(uart_input_buffer_, len);
            hw::VirtualUart::instance().simulate_incoming_data("\n", 1);
            uart_input_buffer_[0] = '\0';
        }
    }
}

void SimulationDashboard::shutdown() noexcept {
    if (window_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        glfwDestroyWindow(window_);
        window_ = nullptr;
        glfwTerminate();
    }
}

} // namespace myrtos::ui
