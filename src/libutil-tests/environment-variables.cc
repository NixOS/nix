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

/* An existing variable whose value is empty is present, not absent. Fails on
   Windows if a zero character count is read as failure. */
TEST(getEnvOs, emptyValueIsPresent)
{
    constexpr auto name = OS_STR("NIX_TEST_GETENVOS_EMPTY");

    ASSERT_EQ(setEnvOs(name, OS_STR("")), 0);

    auto value = getEnvOs(name);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, OS_STR(""));

    EXPECT_EQ(unsetEnvOs(name), 0);
}

TEST(getEnvOs, absentIsNullopt)
{
    constexpr auto name = OS_STR("NIX_TEST_GETENVOS_ABSENT");

    unsetEnvOs(name);

    EXPECT_EQ(getEnvOs(name), std::nullopt);
}

/* `getEnvOsNonEmpty` folds the two together, so it must not distinguish them. */
TEST(getEnvOsNonEmpty, emptyAndAbsentBothNullopt)
{
    constexpr auto name = OS_STR("NIX_TEST_GETENVOS_NONEMPTY");

    ASSERT_EQ(setEnvOs(name, OS_STR("")), 0);
    EXPECT_EQ(getEnvOsNonEmpty(name), std::nullopt);

    EXPECT_EQ(unsetEnvOs(name), 0);
    EXPECT_EQ(getEnvOsNonEmpty(name), std::nullopt);
}

} // namespace nix
