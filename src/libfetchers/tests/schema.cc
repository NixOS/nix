#include "../schema.hh"

#include <gmock/gmock-matchers.h>

namespace nix::fetchers {
    TEST(Schema_String, eq_String) {
        ASSERT_EQ(Schema{Schema::String}, Schema{Schema::String});
    }
    TEST(Schema_String, neq_Int) {
        ASSERT_NE(Schema{Schema::String}, Schema{Schema::Int});
    }
}
