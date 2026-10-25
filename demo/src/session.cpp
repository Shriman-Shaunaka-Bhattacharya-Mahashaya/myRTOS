#include "demo/session.hpp"
#include "kernel/scheduler.hpp"
#include "kernel/queue.hpp"
#include "port/port.hpp"
#include "port/simulation.hpp"
#include "hw/gpio.hpp"
#include "hw/uart.hpp"
#include "hw/vic.hpp"
#include <future>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
namespace myrtos {
struct DemoSession::Run {
    struct Reading { TickType tick; unsigned value; };
    StaticQueue<Reading,16> readings;
    alignas(16) uint8_t stacks[4][DEFAULT_STACK_SIZE_BYTES]{};
    unsigned sample{100};
    TickType period{150};
    char command[128]{};
    size_t command_size{0};
    bool discard_line{false};
    bool gpio_changed{false};
    static void output(const char* text) {
        auto& uart=hw::VirtualUart::instance();
        for (; *text; ++text) if (uart.write_byte(static_cast<uint8_t>(*text)) != Status::Ok) break;
    }
    static void producer(void* p) {
        auto& r=*static_cast<Run*>(p);
        for (;;) {
            Reading value{Scheduler::instance().tick_count(),r.sample++};
            if (r.readings.send(value)!=Status::Ok) output("ERR sensor queue\n");
            Scheduler::instance().sleep_ticks(r.period);
        }
    }
    static void consumer(void* p) {
        auto& r=*static_cast<Run*>(p);
        for (;;) {
            Reading value{};
            if (r.readings.receive(value)==Status::Ok) {
                char line[80]; std::snprintf(line,sizeof line,"[SENSOR] Tick=%u Val=%u\n",value.tick,value.value);
                output(line);
            }
        }
    }
    static void heartbeat(void*) {
        bool on=false;
        for (;;) {
            on=!on;
            hw::VirtualGpio::instance().write_pin(0,on?hw::PinState::High:hw::PinState::Low);
            Scheduler::instance().sleep_ticks(300);
        }
    }
    void execute() {
        command[command_size]='\0';
        for (size_t i=0;i<command_size;++i) command[i]=static_cast<char>(std::toupper(static_cast<unsigned char>(command[i])));
        if (std::strcmp(command,"PING")==0) output("PONG\n");
        else if (std::strcmp(command,"HELP")==0) output("Commands: HELP | PING | STATUS | LED ON | LED OFF | PERIOD 1..10000\n");
        else if (std::strcmp(command,"STATUS")==0) {
            char line[128]; std::snprintf(line,sizeof line,"STATUS tick=%u period=%u LED=%s GPIO2=%s\n",Scheduler::instance().tick_count(),period,
                hw::VirtualGpio::instance().read_pin(1)==hw::PinState::High?"ON":"OFF",
                hw::VirtualGpio::instance().read_pin(2)==hw::PinState::High?"HIGH":"LOW"); output(line);
        } else if (std::strcmp(command,"LED ON")==0 || std::strcmp(command,"LED OFF")==0) {
            const bool on=std::strcmp(command,"LED ON")==0;
            hw::VirtualGpio::instance().write_pin(1,on?hw::PinState::High:hw::PinState::Low);
            output(on?"OK LED ON\n":"OK LED OFF\n");
        } else if (std::strncmp(command,"PERIOD ",7)==0) {
            char* end=nullptr; const auto value=std::strtoul(command+7,&end,10);
            if (end!=command+7 && *end=='\0' && value>=1 && value<=10000) {
                period=static_cast<TickType>(value); output("OK PERIOD (applies after current sleep)\n");
            } else output("ERR PERIOD must be 1..10000\n");
        } else if (command_size) output("ERR unknown command; type HELP\n");
        command_size=0;
    }
    static void inputs(void* p) {
        auto& r=*static_cast<Run*>(p);
        for (;;) {
            if (r.gpio_changed) {
                r.gpio_changed=false;
                const auto state=hw::VirtualGpio::instance().read_pin(2);
                hw::VirtualGpio::instance().write_pin(1,state);
                output(state==hw::PinState::High?"GPIO2 HIGH -> LED ON\n":"GPIO2 LOW -> LED OFF\n");
            }
            uint8_t byte=0;
            // Bounded work per tick prevents a continuous sender starving other tasks.
            for (unsigned i=0;i<512 && hw::VirtualUart::instance().read_byte(&byte)==Status::Ok;++i) {
                if (byte=='\r') continue;
                if (byte=='\n') {
                    if (r.discard_line) { output("ERR command too long\n"); r.discard_line=false; r.command_size=0; }
                    else r.execute();
                } else if (!r.discard_line) {
                    if (r.command_size+1 < sizeof r.command) r.command[r.command_size++]=char(byte);
                    else r.discard_line=true;
                }
            }
            Scheduler::instance().sleep_ticks(1);
        }
    }
    Status configure() {
        auto& gpio=hw::VirtualGpio::instance();
        gpio.configure_pin(0,hw::PinDirection::Output);
        gpio.configure_pin(1,hw::PinDirection::Output);
        gpio.configure_pin(2,hw::PinDirection::Input,hw::InterruptTrigger::BothEdges);
        auto& vic=hw::VirtualInterruptController::instance();
        vic.register_handler(hw::IRQ_GPIO,[](void* p){static_cast<Run*>(p)->gpio_changed=true;},this,3);
        vic.enable_irq(hw::IRQ_GPIO);
        auto& s=Scheduler::instance();
        const char* names[]={"Producer","Consumer","Heartbeat","Inputs"};
        TaskFunction functions[]={producer,consumer,heartbeat,inputs};
        const Priority priorities[]={2,3,5,1};
        for (unsigned i=0;i<4;++i) {
            const auto status=s.create_task(names[i],functions[i],this,priorities[i],stacks[i],sizeof stacks[i]);
            if (status!=Status::Ok) return status;
        }
        output("myRTOS ready. Type HELP. GPIO2 controls LED on pin 1.\n");
        return Status::Ok;
    }
};
DemoSession::DemoSession()=default;
DemoSession::~DemoSession(){ stop(); }
void DemoSession::stop(){ Port::request_stop(); if(worker_.joinable()) worker_.join(); running_=false; }
Status DemoSession::reset(bool paused, TransitionMode mode, unsigned delay_ms){ stop(); return start(paused,mode,delay_ms); }
Status DemoSession::start(bool paused, TransitionMode mode, unsigned delay_ms){
    if(worker_.joinable()) return Status::Error;
    std::promise<Status> initialized; auto ready=initialized.get_future();
    worker_=std::thread([this,paused,mode,delay_ms,&initialized]{
        Status setup;
        {
            CriticalSectionGuard guard;
            // Reset lists before destroying the task storage they reference.
            hw::VirtualInterruptController::instance().reset();
            hw::VirtualGpio::instance().reset();
            hw::VirtualUart::instance().reset();
            setup=Scheduler::instance().init();
            if(setup==Status::Ok){
                run_=std::make_unique<Run>();
                setup=run_->configure();
                if (!SimulationControl::transitions(mode,delay_ms)) setup=Status::InvalidParam;
                if(mode==TransitionMode::Off) SimulationControl::pause(paused);
            }
        }
        running_=setup==Status::Ok;
        initialized.set_value(setup);
        result_=setup==Status::Ok?Scheduler::instance().start():setup;
        running_=false;
    });
    const auto status=ready.get();
    if(status!=Status::Ok) stop();
    else ++generation_;
    return status;
}
}
