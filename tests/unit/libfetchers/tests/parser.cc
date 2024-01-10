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

    auto attrsParser1 = parsers::Attrs(
        [](auto a, auto b, auto c) {
            return std::make_tuple(a, b, c);
        },
        new RequiredAttr("a", String{}),
        new OptionalAttr("b", Int{}),
        new RequiredAttr("c", Bool{})
    );

    TEST(Attrs, schema_attrsParser1) {
        ASSERT_EQ(
            *(attrsParser1.getSchema()),
            Schema {
                Schema::Attrs({
                    {
                        std::string{"a"},
                        Schema::Attrs::Attr {
                            true,
                            std::make_shared<Schema>(Schema::Primitive::String)
                        }
                    },
                    {
                        std::string{"b"},
                        Schema::Attrs::Attr {
                            false,
                            std::make_shared<Schema>(Schema::Primitive::Int)
                        }
                    },
                    {
                        std::string{"c"},
                        Schema::Attrs::Attr {
                            true,
                            std::make_shared<Schema>(Schema::Primitive::Bool)
                        }
                    }
                })
            }
        );
    }
    TEST(Attrs, parse_attrsParser1) {
        ASSERT_EQ(
            attrsParser1.parse(
                nix::fetchers::Attrs{
                    { "a", "hi" },
                    { "b", 101U },
                    { "c", Explicit<bool>{true} }
                }
            ),
            std::make_tuple("hi", 101U, true)
        );
    }
    TEST(Attrs, parse_attrsParser1_missingOptional) {
        ASSERT_EQ(
            attrsParser1.parse(
                nix::fetchers::Attrs{
                    { "a", "hi" },
                    { "c", Explicit<bool>{true} }
                }
            ),
            std::make_tuple("hi", std::nullopt, true)
        );
    }
    TEST(Attrs, parse_attrsParser1_missingRequired) {
        try {
            attrsParser1.parse(
                nix::fetchers::Attrs{
                    { "b", 101U },
                    { "c", Explicit<bool>{true} }
                }
            );
            FAIL();
        } catch (Error & e) {
            ASSERT_THAT(filterANSIEscapes(e.what(), true),
                HasSubstr("while checking fetcher attribute 'a'"));
            ASSERT_THAT(filterANSIEscapes(e.what(), true),
                HasSubstr("required attribute 'a' not found"));
        }
    }
    TEST(Attrs, parse_attrsParser1_wrongType) {
        try {
            attrsParser1.parse(
                nix::fetchers::Attrs{
                    { "a", "hi" },
                    { "b", "hi" },
                    { "c", Explicit<bool>{true} }
                }
            );
            FAIL();
        } catch (Error & e) {
            ASSERT_THAT(filterANSIEscapes(e.what(), true),
                HasSubstr("while checking fetcher attribute 'b'"));
            ASSERT_THAT(filterANSIEscapes(e.what(), true),
                HasSubstr("expected an int, but value is of type string"));
        }
    }
    TEST(Attrs, parse_attrsParser1_extra_before) {
        try {
            attrsParser1.parse(
                nix::fetchers::Attrs{
                    { "0", "hi" },
                    { "a", "hi" },
                    { "b", 101U },
                    { "c", Explicit<bool>{true} }
                }
            );
            FAIL();
        } catch (Error & e) {
            ASSERT_THAT(filterANSIEscapes(e.what(), true),
                HasSubstr("unexpected attribute '0'"));
        }
    }
    TEST(Attrs, parse_attrsParser1_extra_after) {
        try {
            attrsParser1.parse(
                nix::fetchers::Attrs{
                    { "a", "hi" },
                    { "b", 101U },
                    { "c", Explicit<bool>{true} },
                    { "d", "hi" }
                }
            );
            FAIL();
        } catch (Error & e) {
            ASSERT_THAT(filterANSIEscapes(e.what(), true),
                HasSubstr("unexpected attribute 'd'"));
        }
    }
    TEST(Attrs, parse_attrsParser1_extra_between) {
        try {
            attrsParser1.parse(
                nix::fetchers::Attrs{
                    { "a", "hi" },
                    { "aa", "hi" },
                    { "b", 101U },
                    { "c", Explicit<bool>{true} }
                }
            );
            FAIL();
        } catch (Error & e) {
            ASSERT_THAT(filterANSIEscapes(e.what(), true),
                HasSubstr("unexpected attribute 'aa'"));
        }
    }
}
