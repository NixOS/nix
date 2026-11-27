#include <gtest/gtest.h>

#include "nix/util/signals.hh"

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

namespace nix {

/**
 * Clear the interrupt flag set by `triggerInterrupt()`, so that it
 * doesn't affect other tests.
 */
struct ClearInterrupt
{
    ~ClearInterrupt()
    {
        setInterrupted(false);
    }
};

TEST(SignalCallbacks, deregistrationWaitsForRunningCallback)
{
    ClearInterrupt clearInterrupt;

    std::promise<void> started, release;
    auto releaseFuture = release.get_future();
    std::atomic<bool> finished = false;

    auto callback = createInterruptCallback([&]() {
        started.set_value();
        releaseFuture.wait();
        finished = true;
    });

    std::thread trigger([]() { unix::triggerInterrupt(); });
    started.get_future().wait();

    /* Let the callback finish (probably) after deregistration has
       started. */
    std::thread releaser([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        release.set_value();
    });

    callback.reset();
    EXPECT_TRUE(finished);

    trigger.join();
    releaser.join();
}

} // namespace nix
