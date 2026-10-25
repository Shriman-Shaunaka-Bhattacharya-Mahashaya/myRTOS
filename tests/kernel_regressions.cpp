#include "test_support.hpp"
#include "kernel/scheduler.hpp"
#include "kernel/timer.hpp"
#include "kernel/memory_pool.hpp"
#include "kernel/semaphore.hpp"
#include "kernel/queue.hpp"
using namespace myrtos;
int main(){
    CHECK(tick_reached(0,0xFFFFFFFFU));CHECK(!tick_reached(0xFFFFFFFFU,1));
    IntrusiveList a,b;ListNode node; a.push_back(&node);a.push_back(&node);b.remove(&node);
    CHECK(a.size()==1);CHECK(b.empty());a.reset();CHECK(!node.is_linked());
    alignas(void*) uint8_t storage[128]{};MemoryPool pool(storage,64,2);
    auto* block=pool.allocate();CHECK(pool.deallocate(block)==Status::Ok);
    CHECK(pool.deallocate(block)==Status::InvalidParam);CHECK(pool.free_blocks()==2);
    CHECK(pool.deallocate(storage+1)==Status::InvalidParam);
    MemoryPool invalid(storage,9,2);CHECK(invalid.allocate()==nullptr);
    struct Odd {char bytes[9];};StaticMemoryPool<Odd,3> odd;
    auto* x=odd.allocate();auto* y=odd.allocate();CHECK(x!=y);CHECK(odd.deallocate(x)==Status::Ok);CHECK(odd.deallocate(y)==Status::Ok);
    Semaphore sem(0,1);CHECK(sem.acquire(3)==Status::InvalidParam);
    StaticQueue<unsigned,1> queue;unsigned value=0;CHECK(queue.receive(value,3)==Status::InvalidParam);
    auto& s=Scheduler::instance();CHECK(s.init()==Status::Ok);CHECK(s.set_initial_tick(0xFFFFFFFEU)==Status::Ok);
    unsigned once=0,periodic=0;
    SoftwareTimer single("one",2,TimerMode::OneShot,[](void* p){++*static_cast<unsigned*>(p);},&once);
    SoftwareTimer repeating("repeat",1,TimerMode::Periodic,[](void* p){++*static_cast<unsigned*>(p);},&periodic);
    CHECK(single.start()==Status::Ok);CHECK(repeating.start()==Status::Ok);
    s.tick_isr();CHECK(once==0);s.tick_isr();CHECK(once==1);CHECK(!single.is_active());
    s.tick_isr();CHECK(once==1);CHECK(periodic==3);repeating.stop();
    struct CallbackState {SoftwareTimer* timer; unsigned calls;};CallbackState state{nullptr,0};
    SoftwareTimer self("self",1,TimerMode::OneShot,[](void* p){auto& st=*static_cast<CallbackState*>(p);if(++st.calls==1) st.timer->reset();},&state);
    state.timer=&self;self.start();s.tick_isr();s.tick_isr();s.tick_isr();CHECK(state.calls==2);CHECK(!self.is_active());
    {SoftwareTimer scoped("scoped",1,TimerMode::Periodic,[](void*){});scoped.start();}
    CHECK(TimerManager::instance().active_timer_count()==0);
    std::puts("Kernel regressions: timer modes/callback reset/lifetime, rollover, list membership, double free/alignment, invalid blocking passed.");
}
