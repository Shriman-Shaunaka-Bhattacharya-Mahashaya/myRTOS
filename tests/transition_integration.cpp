#include "test_support.hpp"
#include "demo/session.hpp"
#include "kernel/scheduler.hpp"
#include "hw/uart.hpp"
#include <chrono>
#include <thread>
#include <string>
using namespace myrtos;
using namespace std::chrono_literals;
template<class Predicate> void wait_for(Predicate predicate){
    const auto end=std::chrono::steady_clock::now()+3s;
    while(!predicate() && std::chrono::steady_clock::now()<end) std::this_thread::sleep_for(1ms);
    CHECK(predicate());
}
SimulationState held(){
    wait_for([]{return SimulationControl::state().transition_waiting;});
    return SimulationControl::state();
}
void next(uint32_t target){
    const auto sequence=held().transition_sequence;
    CHECK(SimulationControl::next_transition());
    wait_for([&]{const auto s=SimulationControl::state();return s.transition_waiting && s.transition_sequence>sequence;});
    const auto s=SimulationControl::state();
    CHECK(s.transition_sequence==sequence+1);CHECK(s.transition_to==target);
}
int main(){
    DemoSession session;
    CHECK(!SimulationControl::transitions(TransitionMode::Automatic,99));
    CHECK(!SimulationControl::transitions(TransitionMode::Manual,10001));
    CHECK(!SimulationControl::transitions(static_cast<TransitionMode>(99)));
    CHECK(!SimulationControl::next_transition());
    CHECK(session.start(false,TransitionMode::Manual)==Status::Ok);
    auto s=held();CHECK(s.transition_to==4);CHECK(s.transition_tick==0);
    TaskMetrics metrics[MAX_TASKS+1];
    CHECK(Scheduler::instance().get_task_metrics(metrics,MAX_TASKS+1)==5);
    CHECK(metrics[4].state==TaskState::Running);
    CHECK(metrics[1].state==TaskState::Ready);CHECK(metrics[2].state==TaskState::Ready);
    // UI telemetry and input APIs remain usable while the worker is held.
    CHECK(hw::VirtualUart::instance().simulate_incoming_data("PING\n",5)==Status::Ok);
    std::this_thread::sleep_for(150ms);
    CHECK(Scheduler::instance().tick_count()==0);
    CHECK(hw::VirtualUart::instance().is_rx_available());
    CHECK(!SimulationControl::step());
    next(1); // Inputs ran and blocked; Producer is selected but has not produced.
    Scheduler::instance().get_task_metrics(metrics,MAX_TASKS+1);
    CHECK(metrics[4].state==TaskState::Blocked);CHECK(metrics[1].state==TaskState::Running);
    char buffer[512];auto n=hw::VirtualUart::instance().pull_transmitted_data(buffer,sizeof buffer);
    const std::string output(buffer,n);CHECK(output.find("PONG")!=std::string::npos);
    CHECK(output.find("[SENSOR]")==std::string::npos);
    next(2);next(3);next(0);
    CHECK(Scheduler::instance().tick_count()==0);
    // A long teaching hold must not accumulate overdue simulation ticks.
    CHECK(SimulationControl::speed(0.001));
    std::this_thread::sleep_for(1100ms);
    CHECK(SimulationControl::transitions(TransitionMode::Off));
    std::this_thread::sleep_for(100ms);CHECK(Scheduler::instance().tick_count()==0);
    SimulationControl::pause(true);wait_for([]{return SimulationControl::state().paused;});
    CHECK(SimulationControl::speed(1));
    CHECK(SimulationControl::transitions(TransitionMode::Automatic,300));
    s=held();CHECK(s.transition_tick==1);CHECK(s.transition_to==4);
    const auto sequence=s.transition_sequence;
    CHECK(!SimulationControl::next_transition());
    std::this_thread::sleep_for(120ms);CHECK(SimulationControl::state().transition_sequence==sequence);
    wait_for([&]{return SimulationControl::state().transition_sequence>sequence;});
    CHECK(SimulationControl::transitions(TransitionMode::Manual));
    held();
    const auto stop_start=std::chrono::steady_clock::now();
    session.stop();CHECK(std::chrono::steady_clock::now()-stop_start<1s);
    CHECK(!SimulationControl::state().transition_waiting);
    for(int i=0;i<2;++i){
        CHECK(session.reset(false,TransitionMode::Manual)==Status::Ok);
        s=held();CHECK(s.transition_sequence==1);CHECK(s.transition_to==4);CHECK(s.transition_tick==0);
        // Stop before any target context has started, on both host ports.
        session.stop();CHECK(session.result()==Status::Ok);
    }
    CHECK(session.reset(true)==Status::Ok);
    wait_for([]{return SimulationControl::state().paused;});
    CHECK(SimulationControl::state().transition_mode==TransitionMode::Off);
    session.stop();
    std::puts("Teaching transitions: live states, manual/automatic, input, clock rebasing, stop/reset passed.");
}
