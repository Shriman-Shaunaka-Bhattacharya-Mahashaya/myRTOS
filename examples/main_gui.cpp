#include "demo/session.hpp"
#include "ui/dashboard.hpp"
#include <cstring>
#include <iostream>
int main(int argc,char** argv){
    const bool smoke=argc==2 && std::strcmp(argv[1],"--smoke-test")==0;
    if(argc>1 && !smoke){std::cerr<<"Usage: myrtos_dashboard [--smoke-test]\n";return 2;}
    myrtos::ui::SimulationDashboard dashboard;
    if(!dashboard.init()) return 1;
    myrtos::DemoSession session;
    if(session.start()!=myrtos::Status::Ok) return 1;
    dashboard.run(session,smoke?120:0);
    session.stop();
    dashboard.shutdown();
    if(session.result()!=myrtos::Status::Ok) return 1;
    if(smoke){
        if(myrtos::Scheduler::instance().tick_count()==0) return 1;
        std::cout<<"Dashboard smoke test: 120 frames, advancing clock, clean shutdown.\n";
    }
    return 0;
}
