#include "../schema.hh"

#include <gmock/gmock-matchers.h>

namespace nix::fetchers {
    // Equality tests are boilerplaty but crucial for the validity of all tests,
    // which use it.

    TEST(Schema_String, eq_String) {
        ASSERT_EQ(Schema{Schema::String}, Schema{Schema::String});
    }
    TEST(Schema_String, neq_Int) {
        ASSERT_NE(Schema{Schema::String}, Schema{Schema::Int});
    }
    TEST(Schema_String, neq_Attrs) {
        ASSERT_NE(Schema{Schema::String}, Schema{Schema::Attrs{}});
    }
    TEST(Schema_Attrs, eq_Attrs) {
        ASSERT_EQ(Schema{Schema::Attrs{}}, Schema{Schema::Attrs{}});
    }
    TEST(Schema_Attrs, neq_Attrs_attrType) {
        Schema::Attrs a;
        a.attrs.emplace("x", Schema::Attrs::Attr{true, std::make_shared<Schema>(Schema::String)});
        Schema::Attrs b;
        b.attrs.emplace("x", Schema::Attrs::Attr{true, std::make_shared<Schema>(Schema::Int)});
        ASSERT_NE(Schema{a}, Schema{b});
    }
    TEST(Schema_Attrs, neq_Attrs_attrName) {
        Schema::Attrs a;
        a.attrs.emplace("x", Schema::Attrs::Attr{true, std::make_shared<Schema>(Schema::String)});
        Schema::Attrs b;
        b.attrs.emplace("y", Schema::Attrs::Attr{true, std::make_shared<Schema>(Schema::String)});
        ASSERT_NE(Schema{a}, Schema{b});
    }
    TEST(Schema_Attrs, neq_Attrs_required) {
        Schema::Attrs a;
        a.attrs.emplace("x", Schema::Attrs::Attr{true, std::make_shared<Schema>(Schema::String)});
        Schema::Attrs b;
        b.attrs.emplace("x", Schema::Attrs::Attr{false, std::make_shared<Schema>(Schema::String)});
        ASSERT_NE(Schema{a}, Schema{b});
    }
    TEST(Schema_Attrs, neq_Attrs_missing) {
        Schema::Attrs a;
        a.attrs.emplace("x", Schema::Attrs::Attr{true, std::make_shared<Schema>(Schema::String)});
        Schema::Attrs b;
        ASSERT_NE(Schema{a}, Schema{b});
    }

}
