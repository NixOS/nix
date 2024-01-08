#include "../attrs.hh"

#include <gmock/gmock-matchers.h>

namespace nix::fetchers {
    TEST(attrType, string0) {
        ASSERT_EQ(attrType(""), "string");
    }
    TEST(attrType, string1) {
        ASSERT_EQ(attrType("hello"), "string");
    }
    TEST(attrType, bool0) {
        ASSERT_EQ(attrType(Explicit<bool>{false}), "bool");
    }
    TEST(attrType, bool1) {
        ASSERT_EQ(attrType(Explicit<bool>{true}), "bool");
    }
    TEST(attrType, int0) {
        ASSERT_EQ(attrType(0U), "int");
    }
    TEST(attrType, int1) {
        ASSERT_EQ(attrType(1U), "int");
    }
}