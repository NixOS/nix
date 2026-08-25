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

/* `getEnv` is defined in terms of `getEnvOs`, so they must agree. Fails on
   Windows if `getEnv` reads the CRT copy, which `setEnv` does not update. */
TEST(getEnv, agreesWithGetEnvOs)
{
    constexpr auto name = "NIX_TEST_GETENV_AGREE";
    constexpr auto nameOS = OS_STR("NIX_TEST_GETENV_AGREE");

    ASSERT_EQ(setEnv(name, "value"), 0);

    EXPECT_EQ(getEnv(name), "value");
    EXPECT_EQ(getEnvOs(nameOS), OS_STR("value"));

    EXPECT_EQ(unsetEnvOs(nameOS), 0);

    EXPECT_EQ(getEnv(name), std::nullopt);
    EXPECT_EQ(getEnvOs(nameOS), std::nullopt);
}

} // namespace nix
