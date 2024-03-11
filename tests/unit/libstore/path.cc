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
TEST_DONT_PARSE(dot, ".")
TEST_DONT_PARSE(dot_dot, "..")
TEST_DONT_PARSE(dot_dot_dash, "..-1")
TEST_DONT_PARSE(dot_dash, ".-1")
TEST_DONT_PARSE(dot_dot_dash_a, "..-a")
TEST_DONT_PARSE(dot_dash_a, ".-a")

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
TEST_DO_PARSE(dotfile, ".gitignore")
TEST_DO_PARSE(triple_dot_a, "...a")
TEST_DO_PARSE(triple_dot_1, "...1")
TEST_DO_PARSE(triple_dot_dash, "...-")
TEST_DO_PARSE(triple_dot, "...")

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


RC_GTEST_FIXTURE_PROP(
    StorePathTest,
    prop_check_regex_eq_parse,
    ())
{
    static auto nameFuzzer =
        rc::gen::container<std::string>(
            rc::gen::oneOf(
                // alphanum, repeated to weigh heavier
                rc::gen::oneOf(
                    rc::gen::inRange('0', '9'),
                    rc::gen::inRange('a', 'z'),
                    rc::gen::inRange('A', 'Z')
                ),
                // valid symbols
                rc::gen::oneOf(
                    rc::gen::just('+'),
                    rc::gen::just('-'),
                    rc::gen::just('.'),
                    rc::gen::just('_'),
                    rc::gen::just('?'),
                    rc::gen::just('=')
                ),
                // symbols for scary .- and ..- cases, repeated for weight
                rc::gen::just('.'), rc::gen::just('.'),
                rc::gen::just('.'), rc::gen::just('.'),
                rc::gen::just('-'), rc::gen::just('-'),
                // ascii symbol ranges
                rc::gen::oneOf(
                    rc::gen::inRange(' ', '/'),
                    rc::gen::inRange(':', '@'),
                    rc::gen::inRange('[', '`'),
                    rc::gen::inRange('{', '~')
                ),
                // typical whitespace
                rc::gen::oneOf(
                    rc::gen::just(' '),
                    rc::gen::just('\t'),
                    rc::gen::just('\n'),
                    rc::gen::just('\r')
                ),
                // some chance of control codes, non-ascii or other garbage we missed
                rc::gen::inRange('\0', '\xff')
        ));

    auto name = *nameFuzzer;

    std::string path = store->storeDir + "/575s52sh487i0ylmbs9pvi606ljdszr0-" + name;
    bool parsed = false;
    try {
        store->parseStorePath(path);
        parsed = true;
    } catch (const BadStorePath &) {
    }
    RC_ASSERT(parsed == std::regex_match(std::string { name }, nameRegex));
}

#endif

}
