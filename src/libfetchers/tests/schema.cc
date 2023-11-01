#include "../schema.hh"

#include <gmock/gmock-matchers.h>

namespace nix::fetchers {
    TEST(Schema_String, eq_String) {
        ASSERT_EQ(Schema{Schema::String}, Schema{Schema::String});
    }
}
