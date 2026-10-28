#include "nix/util/error.hh"

#include <gtest/gtest.h>

namespace nix {

TEST(SystemError, matchesAnyErrorCondition)
{
    const SystemError error(std::make_error_code(std::errc::permission_denied), "opening file");

    EXPECT_TRUE(error.is(std::errc::permission_denied));
    EXPECT_FALSE(error.is(std::errc::io_error));

    EXPECT_TRUE(error.is(std::errc::permission_denied, std::errc::io_error, std::errc::no_such_process));
    EXPECT_TRUE(error.is(std::errc::io_error, std::errc::permission_denied, std::errc::no_such_process));
    EXPECT_TRUE(error.is(std::errc::io_error, std::errc::no_such_process, std::errc::permission_denied));
    EXPECT_FALSE(error.is(std::errc::io_error, std::errc::no_such_process));
}

} // namespace nix
