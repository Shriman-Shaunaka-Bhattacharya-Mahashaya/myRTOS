#include "test_support.hpp"
#include "demo/session.hpp"
#include "port/simulation.hpp"
#include "kernel/scheduler.hpp"
#include "hw/uart.hpp"
#include "hw/gpio.hpp"
#include <chrono>
#include <thread>
#include <string>
#include <limits>
using namespace myrtos;
using namespace std::chrono_literals;
template<class Predicate> void wait_for(Predicate predicate){
    const auto end=std::chrono::steady_clock::now()+3s;
    while(!predicate() && std::chrono::steady_clock::now()<end) std::this_thread::sleep_for(1ms);
    CHECK(predicate());
}
std::string drain(){char buffer[512];auto n=hw::VirtualUart::instance().pull_transmitted_data(buffer,sizeof buffer);return {buffer,n};}
void step(){CHECK(SimulationControl::step());wait_for([]{return SimulationControl::state().paused;});}
void command(const char* text,const char* reply){
    CHECK(hw::VirtualUart::instance().simulate_incoming_data(text,std::char_traits<char>::length(text))==Status::Ok);
    step(); CHECK(drain().find(reply)!=std::string::npos);
}
int main(){
    DemoSession session;
    CHECK(session.start(true)==Status::Ok);
    wait_for([]{return SimulationControl::state().paused;});
    CHECK(Scheduler::instance().tick_count()==0);
    CHECK(drain().find("[SENSOR]")!=std::string::npos);
    std::this_thread::sleep_for(30ms); CHECK(Scheduler::instance().tick_count()==0);
    command("PING\n","PONG"); CHECK(Scheduler::instance().tick_count()==1);
    command("LED ON\n","OK LED ON"); CHECK(hw::VirtualGpio::instance().read_pin(1)==hw::PinState::High);
    command("LED OFF\n","OK LED OFF"); CHECK(hw::VirtualGpio::instance().read_pin(1)==hw::PinState::Low);
    command("STATUS\n","STATUS tick="); command("HELP\n","Commands:");
    command("PERIOD 5\n","OK PERIOD"); command("PERIOD 0\n","ERR PERIOD"); command("BOGUS\n","ERR unknown");
    auto& gpio=hw::VirtualGpio::instance();
    gpio.simulate_input_change(2,hw::PinState::High); step();
    CHECK(gpio.read_pin(1)==hw::PinState::High); CHECK(drain().find("GPIO2 HIGH -> LED ON")!=std::string::npos);
    gpio.simulate_input_change(2,hw::PinState::Low); step();
    CHECK(gpio.read_pin(1)==hw::PinState::Low); CHECK(drain().find("GPIO2 LOW -> LED OFF")!=std::string::npos);
    const auto base=Scheduler::instance().tick_count();
    CHECK(SimulationControl::step()); CHECK(SimulationControl::step()); CHECK(SimulationControl::step());
    wait_for([]{return SimulationControl::state().paused;});CHECK(Scheduler::instance().tick_count()==base+3);
    std::string oversized(513,'x'); CHECK(hw::VirtualUart::instance().simulate_incoming_data(oversized.data(),oversized.size())==Status::ResourceExhausted);
    CHECK(!hw::VirtualUart::instance().is_rx_available());
    std::string long_line(150,'x');long_line+='\n';command(long_line.c_str(),"ERR command too long");
    CHECK(!SimulationControl::speed(0)); CHECK(!SimulationControl::speed(11));
    CHECK(!SimulationControl::speed(0.00049)); CHECK(!SimulationControl::speed(-1));
    CHECK(!SimulationControl::speed(std::numeric_limits<double>::quiet_NaN()));
    CHECK(!SimulationControl::speed(std::numeric_limits<double>::infinity()));
    for(double multiplier : {0.0005,0.001,0.005}){
        CHECK(SimulationControl::speed(multiplier));
        CHECK(SimulationControl::state().speed==multiplier);
        const auto before=Scheduler::instance().tick_count();
        const auto started=std::chrono::steady_clock::now();
        SimulationControl::pause(false);
        wait_for([&]{return Scheduler::instance().tick_count()>before;});
        CHECK(std::chrono::steady_clock::now()-started>=std::chrono::duration<double>(0.0008/multiplier));
        SimulationControl::pause(true);wait_for([]{return SimulationControl::state().paused;});
        const auto paused=Scheduler::instance().tick_count();
        step();CHECK(Scheduler::instance().tick_count()==paused+1);
    }
    CHECK(SimulationControl::speed(0.01)); CHECK(SimulationControl::state().speed==0.01);
    const auto slow_wall=std::chrono::steady_clock::now();
    const auto slow_start=Scheduler::instance().tick_count();
    SimulationControl::pause(false);
    wait_for([&]{return Scheduler::instance().tick_count()>=slow_start+3;});
    SimulationControl::pause(true);wait_for([]{return SimulationControl::state().paused;});
    // At 0.01x, three ticks take approximately 300 ms; do not require exact host timing.
    CHECK(std::chrono::steady_clock::now()-slow_wall>=250ms);
    const auto slow_ticks=Scheduler::instance().tick_count()-slow_start;
    CHECK(slow_ticks>=3 && slow_ticks<=10);
    const auto paused_tick=Scheduler::instance().tick_count(); step();
    CHECK(Scheduler::instance().tick_count()==paused_tick+1);
    auto measure=[](double speed){
        CHECK(SimulationControl::speed(speed));
        const auto begin=Scheduler::instance().tick_count();
        SimulationControl::pause(false);std::this_thread::sleep_for(150ms);SimulationControl::pause(true);
        wait_for([]{return SimulationControl::state().paused;});
        return Scheduler::instance().tick_count()-begin;
    };
    const auto normal=measure(1),fast=measure(10);
    CHECK(normal>20); CHECK(fast>normal*3);
    TaskMetrics metrics[MAX_TASKS+1];
    const auto metric_count=Scheduler::instance().get_task_metrics(metrics,MAX_TASKS+1);
    CHECK(metric_count==5);
    for(size_t i=1;i<metric_count;++i){
        CHECK(metrics[i].state==TaskState::Blocked);
        CHECK(metrics[i].context_switch_count>0);
        CHECK(metrics[i].last_run_tick<=Scheduler::instance().tick_count());
    }
    SwitchEvent events[2048];uint64_t dropped=0;
    auto count=Scheduler::instance().get_trace(events,2048,dropped);CHECK(count>10);
    for(size_t i=1;i<count;++i) CHECK(events[i].sequence==events[i-1].sequence+1);
    for(int round=0;round<3;++round){
        CHECK(session.reset(true)==Status::Ok);wait_for([]{return SimulationControl::state().paused;});
        CHECK(Scheduler::instance().tick_count()==0);
        const auto reset_count=Scheduler::instance().get_task_metrics(metrics,MAX_TASKS+1);
        for(size_t i=0;i<reset_count;++i) CHECK(metrics[i].last_run_tick==0);
        CHECK(gpio.read_pin(1)==hw::PinState::Low);
        CHECK(drain().find("Val=100")!=std::string::npos);command("PING\n","PONG");
    }
    CHECK(SimulationControl::speed(0.0005));
    SimulationControl::pause(false);std::this_thread::sleep_for(20ms);
    const auto stop_started=std::chrono::steady_clock::now();
    session.stop(); CHECK(std::chrono::steady_clock::now()-stop_started<1s); CHECK(!session.running());CHECK(!SimulationControl::state().running);CHECK(session.result()==Status::Ok);
    std::printf("Demo controls: paused ticks stable, exact step, speed %u/%u ticks, UART/GPIO replies, trace, reset, stop passed.\n",normal,fast);
}
