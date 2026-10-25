#include "test_support.hpp"
#include "kernel/scheduler.hpp"
#include "kernel/queue.hpp"
#include "kernel/mutex.hpp"
#include "port/port.hpp"
#include <thread>
#include <future>
#include <atomic>
#include <chrono>
using namespace myrtos;
struct Scenario {StaticQueue<unsigned,1> queue;Mutex mutex;std::atomic<unsigned> done{0};alignas(16) uint8_t stacks[2][DEFAULT_STACK_SIZE_BYTES]{};};
void sender(void* p){auto& r=*static_cast<Scenario*>(p);auto& s=Scheduler::instance();
    auto begin=s.tick_count();CHECK(r.queue.send(99,5)==Status::Timeout);CHECK(TickType(s.tick_count()-begin)==5);
    CHECK(r.mutex.lock()==Status::Ok);++r.done; // return must release ownership
}
void thief(void* p){auto& r=*static_cast<Scenario*>(p);auto& s=Scheduler::instance();
    for(unsigned i=0;i<7;++i){s.sleep_ticks(1);unsigned value=0;CHECK(r.queue.try_receive(value)==Status::Ok);CHECK(r.queue.try_send(i)==Status::Ok);}
    CHECK(!r.mutex.is_locked());CHECK(r.mutex.lock()==Status::Ok);CHECK(r.mutex.unlock()==Status::Ok);
    auto begin=s.tick_count();CHECK(s.sleep_ticks(5)==Status::Ok);CHECK(TickType(s.tick_count()-begin)==5);++r.done;
}
int main(){Scenario r;CHECK(r.queue.try_send(1)==Status::Ok);std::promise<void> initialized;auto ready=initialized.get_future();
    std::thread worker([&]{auto& s=Scheduler::instance();CHECK(s.init()==Status::Ok);CHECK(s.set_initial_tick(0xFFFFFFFCU)==Status::Ok);
        // Higher-priority thief consumes each free slot before the blocked sender resumes.
        CHECK(s.create_task("sender",sender,&r,3,r.stacks[0],sizeof r.stacks[0])==Status::Ok);
        CHECK(s.create_task("thief",thief,&r,1,r.stacks[1],sizeof r.stacks[1])==Status::Ok);
        initialized.set_value();CHECK(s.start()==Status::Ok);
    });ready.wait();auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(r.done!=2 && std::chrono::steady_clock::now()<limit) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(r.done==2);Port::request_stop();worker.join();
    std::puts("Deadline integration: queue retry preserves original deadline across rollover; sleep and return-owned mutex cleanup passed.");
}
