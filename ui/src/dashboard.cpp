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
#include <fstream>
#include <cstdlib>
#include "port/simulation.hpp"

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
        std::fprintf(stderr, "Failed to initialize GLFW.\n");
        return false;
    }

    // Attempt 1: OpenGL 3.3 Core Profile (Modern desktop default)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    const char* glsl_version = "#version 130";

    // Attempt 2: OpenGL 3.0 (Compatibility profile for Xvfb / software Mesa)
    if (!window_) {
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
        window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    }

    // Attempt 3: OpenGL 2.1 (Universal fallback for containers & virtual machines)
    if (!window_) {
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
        glsl_version = "#version 120";
    }

    if (!window_) {
        const char* desc = nullptr;
        int err = glfwGetError(&desc);
        std::fprintf(stderr, "Failed to create GLFW window (GLFW error %d: %s).\n",
                     err, desc ? desc : "Unknown error");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
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
    ImGui_ImplOpenGL3_Init(glsl_version);

    return true;
}

void SimulationDashboard::run(DemoSession& session, size_t frame_limit) noexcept {
    session_ = &session;
    size_t frames = 0;
    while (!glfwWindowShouldClose(window_) && (frame_limit == 0 || frames < frame_limit)) {
        ++frames;
        if (generation_ != session.generation()) {
            generation_ = session.generation();
            for(size_t i=0;i<MAX_TASKS+1;++i){ previous_switches_[i]=0; activity_until_[i]=0; }
            uart_terminal_history_.clear();
            execution_trace_.clear();
            uart_input_buffer_[0] = '\0';
        }
        task_metric_count_ = Scheduler::instance().get_task_metrics(task_metrics_, MAX_TASKS + 1, &sampled_tick_);
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

        switch_count_ = Scheduler::instance().get_trace(switch_events_, 512, omitted_events_);

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
    const auto state = SimulationControl::state();
    ImGui::TextColored(ImVec4(0.2f,0.8f,1,1), "myRTOS SIMULATOR");
    ImGui::SameLine();
    ImGui::Text("| Tick %u | %s | Run %u",sampled_tick_,
        !session_->running()?"Stopped":state.transition_waiting?"Transition held":state.paused?"Paused":state.pause_requested?"Pausing...":"Running",generation_);
    ImGui::BeginDisabled(!session_->running());
    ImGui::BeginDisabled(state.transition_mode!=TransitionMode::Off);
    if(ImGui::Button(state.pause_requested?"Resume":"Pause",ImVec2(90,28))) SimulationControl::pause(!state.pause_requested);
    ImGui::SameLine();
    ImGui::BeginDisabled(!state.paused);
    if(ImGui::Button("Step 1 tick",ImVec2(100,28))) SimulationControl::step();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::EndDisabled();
    const double speeds[]={0.0005,0.001,0.005,0.01,0.05,0.1,0.25,0.5,1,2,5,10};
    const char* labels[]={"0.0005x","0.001x","0.005x","0.01x","0.05x","0.1x","0.25x","0.5x","1x","2x","5x","10x"};
    char selected[24]; std::snprintf(selected,sizeof selected,"%.2gx",state.speed);
    ImGui::SetNextItemWidth(120);
    if(ImGui::BeginCombo("Speed",selected)){
        for(size_t i=0;i<sizeof(speeds)/sizeof(speeds[0]);++i) if(ImGui::Selectable(labels[i],state.speed==speeds[i])) SimulationControl::speed(speeds[i]);
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if(ImGui::Button("Stop",ImVec2(70,28))) session_->stop();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if(ImGui::Button("Reset",ImVec2(70,28))) {
        const auto result=session_->reset(true,state.transition_mode,state.transition_delay_ms);
        feedback_=result==Status::Ok?"Reset complete. Use the active clock or teaching controls to continue.":"Reset failed.";
    }
    ImGui::SameLine();
    if(ImGui::Button("Export trace")) {
        const char* path=std::getenv("MYRTOS_TRACE_PATH");
        std::ofstream file(path ? path : "trace.csv");
        file<<"sequence,tick,from_task,to_task\n";
        for(size_t i=0;i<switch_count_;++i){const auto& e=switch_events_[i];file<<e.sequence<<','<<e.tick<<','<<e.from<<','<<e.to<<'\n';}
        feedback_=file?(path?"Trace ready. Use Download trace above the browser desktop.":"Saved displayed events to trace.csv"):"Could not write trace.csv";
    }
    ImGui::BeginDisabled(!session_->running());
    const char* modes[]={"Off","Manual","Automatic"};
    int mode=static_cast<int>(state.transition_mode);
    ImGui::SetNextItemWidth(130);
    if(ImGui::Combo("Teaching mode",&mode,modes,3))
        SimulationControl::transitions(static_cast<TransitionMode>(mode),state.transition_delay_ms);
    ImGui::SameLine();
    ImGui::BeginDisabled(state.transition_mode!=TransitionMode::Manual || !state.transition_waiting);
    if(ImGui::Button("Next transition")) SimulationControl::next_transition();
    ImGui::EndDisabled();
    if(state.transition_mode==TransitionMode::Automatic){
        ImGui::SameLine(); float delay=state.transition_delay_ms/1000.0f;
        ImGui::SetNextItemWidth(150);
        if(ImGui::SliderFloat("Hold seconds",&delay,0.1f,10.0f,"%.1f"))
            SimulationControl::transitions(state.transition_mode,static_cast<unsigned>(delay*1000.0f));
    }
    ImGui::EndDisabled();
    if(state.transition_mode!=TransitionMode::Off){
        ImGui::TextWrapped("Live teaching: holds committed task switches before the selected task executes. Clock Pause/Step are available when teaching is Off. Reset keeps teaching mode.");
        if(state.transition_waiting) ImGui::Text("Held #%llu at tick %u: task %u -> %u (selected).",
            static_cast<unsigned long long>(state.transition_sequence),state.transition_tick,state.transition_from,state.transition_to);
    }
    ImGui::TextWrapped("Demo: pin 0 = heartbeat; pin 2 input controls pin 1 LED. UART: PING, HELP, STATUS, LED ON/OFF, PERIOD 1..10000.");
    if(state.running && state.pause_requested) ImGui::TextWrapped("Inputs wait for Resume or Step. Step advances one tick and lets ready tasks finish their work.");
    if(!feedback_.empty()) ImGui::TextWrapped("%s",feedback_.c_str());
}

void SimulationDashboard::render_task_table() noexcept {
    ImGui::TextColored(ImVec4(0.4f,1,0.4f,1),"Tasks");
    const auto control=SimulationControl::state();
    const double now=ImGui::GetTime();
    if(ImGui::BeginTable("TaskTable",7,ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchProp)){
        for(auto label : {"ID","Name","State now","Priority","Switches","Dispatch tick","Activity"}) ImGui::TableSetupColumn(label);
        ImGui::TableHeadersRow();
        for(size_t i=0;i<task_metric_count_;++i){
            const auto& m=task_metrics_[i];
            const char* states[]={"Ready","Running","Blocked","Suspended","Terminated"};
            if(previous_switches_[i]!=m.context_switch_count){
                previous_switches_[i]=m.context_switch_count; activity_until_[i]=now+0.8;
            }
            const bool held=control.transition_waiting && m.id==control.transition_to;
            const bool recent=now<activity_until_[i] && session_->running();
            ImGui::TableNextRow();
            if((recent || held) && m.id!=0) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,IM_COL32(35,85,50,150));
            ImGui::TableNextColumn();ImGui::Text("%u",m.id);
            ImGui::TableNextColumn();ImGui::TextUnformatted(m.name);
            ImGui::TableNextColumn();
            // Idle remains the selected kernel context while the host waits.
            const char* state=states[static_cast<unsigned>(m.state)];
            if(m.state==TaskState::Running && control.transition_waiting && m.id==control.transition_to) state="Running (held)";
            else if(m.id==0 && m.state==TaskState::Running) state=control.paused?"Paused":"Waiting clock";
            ImGui::TextUnformatted(state);
            ImGui::TableNextColumn();ImGui::Text("%u",m.current_priority);
            ImGui::TableNextColumn();ImGui::Text("%u",m.context_switch_count);
            ImGui::TableNextColumn();
            if(m.context_switch_count) ImGui::Text("%u",m.last_run_tick); else ImGui::TextUnformatted("--");
            ImGui::TableNextColumn();
            if(held) ImGui::TextColored(ImVec4(0.5f,1,0.6f,1),"Selected");
            else if(recent) ImGui::TextColored(ImVec4(0.5f,1,0.6f,1),"Ran recently"); else ImGui::TextUnformatted("--");
        }
        ImGui::EndTable();
    }
    ImGui::TextWrapped("Blocked = waiting, not stuck. Tasks run briefly between frames. Green rows show dispatches seen within 0.8 seconds; Dispatch tick records scheduler selection; a held task has not resumed yet.");
    ImGui::TextWrapped("Idle waits for the clock when tasks are blocked. Lower priority number runs first.");
}

void SimulationDashboard::render_execution_timeline() noexcept {
    ImGui::TextColored(ImVec4(0.4f,1,0.4f,1),"Scheduler switch events");
    ImGui::TextWrapped("%zu recorded events shown; %llu older events omitted. Each row is an actual switch.",switch_count_,static_cast<unsigned long long>(omitted_events_));
    ImGui::BeginChild("SwitchEvents",ImVec2(0,220),true);
    if(ImGui::BeginTable("Events",3,ImGuiTableFlags_RowBg|ImGuiTableFlags_Borders)){
        ImGui::TableSetupColumn("Tick");ImGui::TableSetupColumn("From ID");ImGui::TableSetupColumn("To ID");ImGui::TableHeadersRow();
        ImGuiListClipper clipper;clipper.Begin(static_cast<int>(switch_count_));
        while(clipper.Step()) for(int row=clipper.DisplayStart;row<clipper.DisplayEnd;++row){
            const auto& e=switch_events_[switch_count_-1-static_cast<size_t>(row)];
            ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::Text("%u",e.tick);
            ImGui::TableNextColumn();ImGui::Text("%u",e.from);ImGui::TableNextColumn();ImGui::Text("%u",e.to);
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

void SimulationDashboard::render_virtual_hardware_panel() noexcept {
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Virtual Hardware Peripherals (GPIO & Interrupts)");

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImGui::BeginDisabled(!session_->running());
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
    ImGui::EndDisabled();
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

    ImGui::BeginDisabled(!session_->running());
    // Input Bar
    ImGui::PushItemWidth(-70.0f);
    bool submit = ImGui::InputText("##UartInput", uart_input_buffer_, sizeof(uart_input_buffer_),
                                  ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopItemWidth();

    ImGui::SameLine();
    if (ImGui::Button("Send", ImVec2(60, 0)) || submit) {
        if (uart_input_buffer_[0] != '\0') {
            size_t len = std::strlen(uart_input_buffer_);
            char packet[257];
            std::memcpy(packet,uart_input_buffer_,len); packet[len]='\n';
            if(hw::VirtualUart::instance().simulate_incoming_data(packet,len+1)==Status::Ok){
                uart_input_buffer_[0]='\0'; feedback_="Command queued.";
            } else feedback_="Input buffer full. Resume or step to process queued commands, then retry.";
        }
    }
    ImGui::EndDisabled();
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
