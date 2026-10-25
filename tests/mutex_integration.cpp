#include "test_support.hpp"
#include "kernel/scheduler.hpp"
#include "kernel/mutex.hpp"
#include "port/port.hpp"
#include <atomic>
#include <future>
#include <thread>
#include <chrono>
using namespace myrtos;
struct Scenario {
    Mutex a,b; bool chain; TaskId low_id{},mid_id{};
    std::atomic<unsigned> done{0};
    alignas(16) uint8_t stacks[4][DEFAULT_STACK_SIZE_BYTES]{};
};
void high(void* p){auto& r=*static_cast<Scenario*>(p);auto& s=Scheduler::instance();s.sleep_ticks(2);
    if(r.chain){CHECK(r.b.lock()==Status::Ok);CHECK(r.b.owner()==s.current_task());CHECK(r.b.unlock()==Status::Ok);}
    else CHECK(r.a.lock(3)==Status::Timeout);
    ++r.done;
}
void middle(void* p){auto& r=*static_cast<Scenario*>(p);auto& s=Scheduler::instance();
    if(r.chain){CHECK(r.b.lock()==Status::Ok);s.sleep_ticks(1);CHECK(r.a.lock()==Status::Ok);CHECK(r.a.unlock()==Status::Ok);CHECK(r.b.unlock()==Status::Ok);}
    else {s.sleep_ticks(1);CHECK(r.b.lock()==Status::Ok);CHECK(r.b.owner()==s.current_task());CHECK(r.b.unlock()==Status::Ok);}
    ++r.done;
}
void low(void* p){auto& r=*static_cast<Scenario*>(p);auto& s=Scheduler::instance();
    CHECK(r.a.lock()==Status::Ok);CHECK(r.a.lock()==Status::Ok);CHECK(r.a.lock_count()==2);CHECK(r.a.unlock()==Status::Ok);
    if(!r.chain) CHECK(r.b.lock()==Status::Ok);
    s.sleep_ticks(r.chain?5:7);
    CHECK(s.current_task()->current_priority==(r.chain?1:3));
    if(!r.chain) {CHECK(r.b.unlock()==Status::Ok);CHECK(s.current_task()->current_priority==7);}
    CHECK(r.a.unlock()==Status::Ok);CHECK(s.current_task()->current_priority==7);++r.done;
}
Priority priority(TaskId id){TaskMetrics tasks[MAX_TASKS+1];auto n=Scheduler::instance().get_task_metrics(tasks,MAX_TASKS+1);
    for(size_t i=0;i<n;++i) if(tasks[i].id==id) return tasks[i].current_priority;
    CHECK(false);return 31;
}
void observer(void* p){auto& r=*static_cast<Scenario*>(p);auto& s=Scheduler::instance();s.sleep_ticks(3);
    CHECK(priority(r.low_id)==1);if(r.chain) CHECK(priority(r.mid_id)==1);
    if(!r.chain){s.sleep_ticks(3);CHECK(priority(r.low_id)==3);}
    ++r.done;
}
int main(){
    for(bool chain:{false,true}){
        Scenario r{};r.chain=chain;std::promise<void> initialized;auto ready=initialized.get_future();
        std::thread worker([&]{auto& s=Scheduler::instance();CHECK(s.init()==Status::Ok);
            CHECK(s.create_task("high",high,&r,1,r.stacks[0],sizeof r.stacks[0])==Status::Ok);
            CHECK(s.create_task("middle",middle,&r,3,r.stacks[1],sizeof r.stacks[1],&r.mid_id)==Status::Ok);
            CHECK(s.create_task("low",low,&r,7,r.stacks[2],sizeof r.stacks[2],&r.low_id)==Status::Ok);
            CHECK(s.create_task("observer",observer,&r,4,r.stacks[3],sizeof r.stacks[3])==Status::Ok);
            initialized.set_value();CHECK(s.start()==Status::Ok);
        });ready.wait();
        auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(r.done.load()!=4 && std::chrono::steady_clock::now()<limit)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        CHECK(r.done.load()==4);Port::request_stop();worker.join();CHECK(!r.a.is_locked());CHECK(!r.b.is_locked());
    }
    std::puts("Mutex integration: recursion, ownership handoff, multiple donations, timeout restoration and transitive inheritance passed.");
}
