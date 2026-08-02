#include "nix/util/current-process.hh"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace nix {

TEST(getSelfExe, works)
{
    auto self = getSelfExe();

    ASSERT_TRUE(self.has_value());
    ASSERT_TRUE(exists(*self));
    ASSERT_TRUE(is_regular_file(*self));
    ASSERT_THAT(self->filename().string(), ::testing::HasSubstr("nix-util-tests"));
}

} // namespace nix
