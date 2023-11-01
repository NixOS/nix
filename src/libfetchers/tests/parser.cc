#include "../parser.hh"
#include "../schema.hh"

#include <gmock/gmock-matchers.h>

using namespace testing;

namespace nix::fetchers {
    using namespace parsers;

    TEST(String, example1) {
        ASSERT_EQ(String{}.parse("hi"), "hi");
    }
    TEST(String, intThrows) {
        try {
            String{}.parse(1U);
            FAIL();
        } catch (Error & e) {
            ASSERT_THAT(e.what(), HasSubstr("expected a string, but value is of type int"));
        }
    }
    TEST(String, schema) {
        ASSERT_EQ(
            *(String{}.getSchema()),
            Schema { Schema::Primitive::String }
        );
    }
}
