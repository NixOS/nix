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

    TEST(Int, example1) {
        ASSERT_EQ(Int{}.parse(1U), 1U);
    }
    TEST(Int, stringThrows) {
        try {
            Int{}.parse("hi");
            FAIL();
        } catch (Error & e) {
            ASSERT_THAT(e.what(), HasSubstr("expected an int, but value is of type string"));
        }
    }
    TEST(Int, schema) {
        ASSERT_EQ(
            *(Int{}.getSchema()),
            Schema { Schema::Primitive::Int }
        );
    }

    TEST(Bool, example1) {
        ASSERT_EQ(Bool{}.parse(Explicit<bool>{true}), true);
    }
    TEST(Bool, stringThrows) {
        try {
            Bool{}.parse("hi");
            FAIL();
        } catch (Error & e) {
            ASSERT_THAT(e.what(), HasSubstr("expected a bool, but value is of type string"));
        }
    }
}
