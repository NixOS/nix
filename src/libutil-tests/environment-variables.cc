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
