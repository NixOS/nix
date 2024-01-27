#include <regex>

#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include "path-regex.hh"
#include "store-api.hh"

#include "tests/hash.hh"
#include "tests/libstore.hh"
#include "tests/path.hh"

namespace nix {

#define STORE_DIR "/nix/store/"
#define HASH_PART "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q"

class StorePathTest : public LibStoreTest
{
};

static std::regex nameRegex { std::string { nameRegexStr } };

#define TEST_DONT_PARSE(NAME, STR)                           \
    TEST_F(StorePathTest, bad_ ## NAME) {                    \
        std::string_view str =                               \
            STORE_DIR HASH_PART "-" STR;                     \
        ASSERT_THROW(                                        \
            store->parseStorePath(str),                      \
            BadStorePath);                                   \
        std::string name { STR };                            \
        EXPECT_FALSE(std::regex_match(name, nameRegex));     \
    }

TEST_DONT_PARSE(empty, "")
TEST_DONT_PARSE(garbage, "&*()")
TEST_DONT_PARSE(double_star, "**")
TEST_DONT_PARSE(star_first, "*,foo")
TEST_DONT_PARSE(star_second, "foo,*")
TEST_DONT_PARSE(bang, "foo!o")

#undef TEST_DONT_PARSE

#define TEST_DO_PARSE(NAME, STR)                             \
    TEST_F(StorePathTest, good_ ## NAME) {                   \
        std::string_view str =                               \
            STORE_DIR HASH_PART "-" STR;                     \
        auto p = store->parseStorePath(str);                 \
        std::string name { p.name() };                       \
        EXPECT_TRUE(std::regex_match(name, nameRegex));      \
    }

// 0-9 a-z A-Z + - . _ ? =

TEST_DO_PARSE(numbers, "02345")
TEST_DO_PARSE(lower_case, "foo")
TEST_DO_PARSE(upper_case, "FOO")
TEST_DO_PARSE(plus, "foo+bar")
TEST_DO_PARSE(dash, "foo-dev")
TEST_DO_PARSE(underscore, "foo_bar")
TEST_DO_PARSE(period, "foo.txt")
TEST_DO_PARSE(question_mark, "foo?why")
TEST_DO_PARSE(equals_sign, "foo=foo")

#undef TEST_DO_PARSE

#ifndef COVERAGE

RC_GTEST_FIXTURE_PROP(
    StorePathTest,
    prop_regex_accept,
    (const StorePath & p))
{
    RC_ASSERT(std::regex_match(std::string { p.name() }, nameRegex));
}

RC_GTEST_FIXTURE_PROP(
    StorePathTest,
    prop_round_rip,
    (const StorePath & p))
{
    RC_ASSERT(p == store->parseStorePath(store->printStorePath(p)));
}

#endif

}
