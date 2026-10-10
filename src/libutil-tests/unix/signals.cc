#include "nix/util/processes.hh"
#include "nix/util/signals.hh"

#include <gtest/gtest.h>

#include <latch>
#include <memory>
#include <thread>
#include <unistd.h>

namespace nix {

/* Isolate the process-wide interrupt flag and registrations from other tests.
   Bound each child so a callback registration or destruction cannot hang the
   test suite. Assertions in the child must also fail the parent test. */
static void inChildProcess(fun<void()> test)
{
    Pid child = startProcess([&] {
        alarm(10);
        test();
        _exit(::testing::Test::HasFailure() ? 1 : 0);
    });

    EXPECT_EQ(child.wait(false), 0);
}

TEST(SignalCallbacks, registrationsAreProcessLocal)
{
    inChildProcess([] {
        int parentCalls = 0;
        auto parentCallback = createInterruptCallback([&] { ++parentCalls; });

        inChildProcess([&] {
            unix::triggerInterrupt();
            EXPECT_EQ(parentCalls, 0);

            int childCalls = 0;
            auto childCallback = createInterruptCallback([&] { ++childCalls; });

            unix::triggerInterrupt();
            EXPECT_EQ(parentCalls, 0);
            EXPECT_EQ(childCalls, 1);

            childCallback.reset();
            unix::triggerInterrupt();
            EXPECT_EQ(childCalls, 1);
        });

        unix::triggerInterrupt();
        EXPECT_EQ(parentCalls, 1);
    });
}

TEST(SignalCallbacks, inheritedHandleDoesNotRemoveChildRegistration)
{
    inChildProcess([] {
        auto parentCallback = createInterruptCallback([] {});

        inChildProcess([&] {
            int childCalls = 0;
            auto childCallback = createInterruptCallback([&] { ++childCalls; });

            // Destroying the parent's first registration must not remove the
            // child's first registration, even if their identifiers coincide.
            parentCallback.reset();
            unix::triggerInterrupt();
            EXPECT_EQ(childCalls, 1);
        });
    });
}

struct BlockingCapture
{
    std::latch & destroying;
    std::latch & finish;

    ~BlockingCapture()
    {
        destroying.count_down();
        finish.wait();
    }
};

TEST(SignalCallbacks, childCanRegisterWhileParentRemovesCallback)
{
    inChildProcess([] {
        std::latch destroying{1};
        std::latch finish{1};
        auto callback = createInterruptCallback([capture = std::make_shared<BlockingCapture>(destroying, finish)] {});

        std::thread remover([&] { callback.reset(); });
        destroying.wait();

        // Hold callback removal in progress across the fork through a user's
        // capture destructor. This exercises the inherited-lock deadlock without
        // accessing the registry or depending on signal delivery timing.
        Pid child = startProcess([] {
            alarm(5);
            auto callback = createInterruptCallback([] {});
            callback.reset();
            _exit(0);
        });

        finish.count_down();
        remover.join();

        EXPECT_EQ(child.wait(false), 0);
    });
}

} // namespace nix
