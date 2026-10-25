#pragma once
#include <cstdint>
namespace myrtos {
enum class TransitionMode { Off, Manual, Automatic };
struct SimulationState {
    bool running; bool pause_requested; bool paused; double speed; uint32_t pending_steps;
    TransitionMode transition_mode{TransitionMode::Off};
    unsigned transition_delay_ms{2000};
    bool transition_waiting{false};
    uint64_t transition_sequence{0};
    uint32_t transition_from{0}, transition_to{0}, transition_tick{0};
};
// Controls are acknowledged at an idle boundary after ready work has settled.
class SimulationControl {
public:
    static SimulationState state() noexcept;
    static void pause(bool paused) noexcept;
    static bool step() noexcept;
    static bool speed(double multiplier) noexcept;
    // Teaching gates hold a committed dispatch before the target executes.
    static bool transitions(TransitionMode mode, unsigned delay_ms = 2000) noexcept;
    static bool next_transition() noexcept;
};
}
