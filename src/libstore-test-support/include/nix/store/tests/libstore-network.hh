#pragma once
/// @file

#include <gtest/gtest.h>

namespace nix::testing {

/**
 * Whether to run network tests. This is global so that the test harness can
 * enable this by default if we can run tests in isolation.
 */
extern bool networkTestsAvailable;

/**
 * Set up network tests and, if on linux, create a new network namespace for
 * tests with a loopback interface. This is to avoid binding to ports in the
 * host's namespace.
 */
void setupNetworkTests();

class LibStoreNetworkTest : public virtual ::testing::Test
{
protected:
    void SetUp() override
    {
        if (networkTestsAvailable)
            return;
        static bool warned = false;
        if (!warned) {
            warned = true;
            GTEST_SKIP()
                << "Network tests not enabled by default without user namespaces, use NIX_TEST_FORCE_NETWORK_TESTS=1 to override";
        } else {
            GTEST_SKIP();
        }
    }
};

} // namespace nix::testing
