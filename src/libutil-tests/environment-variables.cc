#include "nix/util/environment-variables.hh"

#include <gtest/gtest.h>

namespace nix {

/* The header says "like POSIX `setenv`", so success is 0. On Windows these wrap
   `SetEnvironmentVariable`, which reports success as nonzero. */
TEST(setEnv, returnsZeroOnSuccess)
{
    EXPECT_EQ(setEnv("NIX_TEST_SETENV_RET", "value"), 0);
    EXPECT_EQ(unsetEnvOs(OS_STR("NIX_TEST_SETENV_RET")), 0);
}

TEST(setEnvOs, returnsZeroOnSuccess)
{
    EXPECT_EQ(setEnvOs(OS_STR("NIX_TEST_SETENVOS_RET"), OS_STR("value")), 0);
    EXPECT_EQ(unsetEnvOs(OS_STR("NIX_TEST_SETENVOS_RET")), 0);
}

} // namespace nix
