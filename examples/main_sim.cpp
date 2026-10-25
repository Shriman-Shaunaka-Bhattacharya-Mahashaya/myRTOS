#include "demo/session.hpp"
#include "hw/uart.hpp"
#include <csignal>
#include <cstdio>
#include <thread>
#include <chrono>
namespace { volatile std::sig_atomic_t stop_requested=0; void stop(int){stop_requested=1;} }
int main(){
    std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
    myrtos::DemoSession session;
    if(session.start()!=myrtos::Status::Ok) return 1;
    std::puts("myRTOS terminal demo. Press Ctrl+C to stop. Use the dashboard for interactive inputs.");
    while(!stop_requested && session.running()){
        char buffer[512];auto count=myrtos::hw::VirtualUart::instance().pull_transmitted_data(buffer,sizeof buffer);
        if(count){std::fwrite(buffer,1,count,stdout);std::fflush(stdout);}
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    session.stop();return session.result()==myrtos::Status::Ok?0:1;
}
