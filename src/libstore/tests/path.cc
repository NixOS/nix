#include <regex>

#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include "nix/store/path-regex.hh"
#include "nix/store/store-api.hh"

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

// For rapidcheck
void showValue(const StorePath & p, std::ostream & os) {
    os << p.to_string();
}

}

namespace rc {
using namespace nix;

Gen<StorePathName> Arbitrary<StorePathName>::arbitrary()
{
    auto len = *gen::inRange<size_t>(
        1,
        StorePath::MaxPathLen - std::string_view { HASH_PART }.size());

    std::string pre;
    pre.reserve(len);

    for (size_t c = 0; c < len; ++c) {
        switch (auto i = *gen::inRange<uint8_t>(0, 10 + 2 * 26 + 6)) {
            case 0 ... 9:
                pre += '0' + i;
            case 10 ... 35:
                pre += 'A' + (i - 10);
                break;
            case 36 ... 61:
                pre += 'a' + (i - 36);
                break;
            case 62:
                pre += '+';
                break;
            case 63:
                pre += '-';
                break;
            case 64:
                pre += '.';
                break;
            case 65:
                pre += '_';
                break;
            case 66:
                pre += '?';
                break;
            case 67:
                pre += '=';
                break;
            default:
                assert(false);
        }
    }

    return gen::just(StorePathName {
        .name = std::move(pre),
    });
}

Gen<StorePath> Arbitrary<StorePath>::arbitrary()
{
    return gen::just(StorePath {
        *gen::arbitrary<Hash>(),
        (*gen::arbitrary<StorePathName>()).name,
    });
}

} // namespace rc

namespace nix {

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

}
