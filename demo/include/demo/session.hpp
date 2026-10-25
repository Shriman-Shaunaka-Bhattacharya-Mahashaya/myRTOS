#pragma once
#include "kernel/types.hpp"
#include "port/simulation.hpp"
#include <memory>
#include <thread>
#include <atomic>
namespace myrtos {
// Owns all task data until after the worker has stopped. UI calls lifecycle methods.
class DemoSession {
public:
    DemoSession();
    ~DemoSession();
    DemoSession(const DemoSession&) = delete;
    DemoSession& operator=(const DemoSession&) = delete;
    Status start(bool paused = false, TransitionMode mode = TransitionMode::Off, unsigned delay_ms = 2000);
    void stop();
    Status reset(bool paused = false, TransitionMode mode = TransitionMode::Off, unsigned delay_ms = 2000);
    bool running() const noexcept { return running_.load(); }
    Status result() const noexcept { return result_.load(); }
    unsigned generation() const noexcept { return generation_; }
private:
    struct Run;
    std::unique_ptr<Run> run_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<Status> result_{Status::Ok};
    unsigned generation_{0};
};
}
