#include "nix/util/environment-variables.hh"

#include <gtest/gtest.h>

namespace nix {

/* On Windows these wrap `SetEnvironmentVariable`, which reports success as
   nonzero, unlike POSIX `setenv`. */
TEST(setEnv, succeeds)
{
    EXPECT_NO_THROW(setEnv("NIX_TEST_SETENV_RET", "value"));
    EXPECT_NO_THROW(unsetEnvOs(OS_STR("NIX_TEST_SETENV_RET")));
}

TEST(setEnvOs, succeeds)
{
    EXPECT_NO_THROW(setEnvOs(OS_STR("NIX_TEST_SETENVOS_RET"), OS_STR("value")));
    EXPECT_NO_THROW(unsetEnvOs(OS_STR("NIX_TEST_SETENVOS_RET")));
}

/* An existing variable whose value is empty is present, not absent. Fails on
   Windows if a zero character count is read as failure. */
TEST(getEnvOs, emptyValueIsPresent)
{
    constexpr auto name = OS_STR("NIX_TEST_GETENVOS_EMPTY");

    ASSERT_NO_THROW(setEnvOs(name, OS_STR("")));

    auto value = getEnvOs(name);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, OS_STR(""));

    EXPECT_NO_THROW(unsetEnvOs(name));
}

TEST(getEnvOs, absentIsNullopt)
{
    constexpr auto name = OS_STR("NIX_TEST_GETENVOS_ABSENT");

    /* Unsetting a variable that is not set succeeds, as with POSIX `unsetenv`. */
    ASSERT_NO_THROW(unsetEnvOs(name));

    EXPECT_EQ(getEnvOs(name), std::nullopt);
}

/* `getEnvOsNonEmpty` folds the two together, so it must not distinguish them. */
TEST(getEnvOsNonEmpty, emptyAndAbsentBothNullopt)
{
    constexpr auto name = OS_STR("NIX_TEST_GETENVOS_NONEMPTY");

    ASSERT_NO_THROW(setEnvOs(name, OS_STR("")));
    EXPECT_EQ(getEnvOsNonEmpty(name), std::nullopt);

    EXPECT_NO_THROW(unsetEnvOs(name));
    EXPECT_EQ(getEnvOsNonEmpty(name), std::nullopt);
}

/* `getEnv` is defined in terms of `getEnvOs`, so they must agree. Fails on
   Windows if `getEnv` reads the CRT copy, which `setEnv` does not update. */
TEST(getEnv, agreesWithGetEnvOs)
{
    constexpr auto name = "NIX_TEST_GETENV_AGREE";
    constexpr auto nameOS = OS_STR("NIX_TEST_GETENV_AGREE");

    ASSERT_NO_THROW(setEnv(name, "value"));

    EXPECT_EQ(getEnv(name), "value");
    EXPECT_EQ(getEnvOs(nameOS), OS_STR("value"));

    EXPECT_NO_THROW(unsetEnvOs(nameOS));

    EXPECT_EQ(getEnv(name), std::nullopt);
    EXPECT_EQ(getEnvOs(nameOS), std::nullopt);
}

} // namespace nix
