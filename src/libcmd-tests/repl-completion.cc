#include <gtest/gtest.h>
#include <string>

#include "nix/cmd/repl-completion.hh"

namespace nix {

/* ----------------------------------------------------------------------------
 * findLastUnquotedDot (parametrised TEST_P suite)
 * --------------------------------------------------------------------------*/

struct FindLastUnquotedDotCase
{
    std::string input;
    size_t expected;
};

class FindLastUnquotedDotTest : public ::testing::TestWithParam<FindLastUnquotedDotCase>
{};

TEST_P(FindLastUnquotedDotTest, findsCorrectPosition)
{
    const auto & [input, expected] = GetParam();
    EXPECT_EQ(findLastUnquotedDot(input), expected);
}

INSTANTIATE_TEST_SUITE_P(
    FindLastUnquotedDot,
    FindLastUnquotedDotTest,
    ::testing::Values(
        // No dot at all
        FindLastUnquotedDotCase{"foobar", std::string::npos},
        // Empty string
        FindLastUnquotedDotCase{"", std::string::npos},
        // Single dot
        FindLastUnquotedDotCase{"foo.bar", 3u},
        // Multiple dots — returns last
        FindLastUnquotedDotCase{"a.b.c", 3u},
        // Dot inside quotes should be ignored
        FindLastUnquotedDotCase{"a.\"b.c\"", 1u},
        // All dots inside quotes — no unquoted dot
        FindLastUnquotedDotCase{"\"a.b.c\"", std::string::npos},
        // Dot after quoted segment: a."b.c".d — last unquoted dot before 'd'
        FindLastUnquotedDotCase{"a.\"b.c\".d", 7u},
        // Mixed quoted and unquoted: foo."bar.baz".qux."x.y"
        FindLastUnquotedDotCase{"foo.\"bar.baz\".qux.\"x.y\"", 17u},
        // Only a dot
        FindLastUnquotedDotCase{".", 0u},
        // Dot at start
        FindLastUnquotedDotCase{".foo", 0u},
        // Dot at end
        FindLastUnquotedDotCase{"foo.", 3u},
        // Unclosed quote hides trailing dot
        FindLastUnquotedDotCase{"foo.\"bar.", 3u}));

/* ----------------------------------------------------------------------------
 * formatAttrName (parametrised TEST_P suite)
 * --------------------------------------------------------------------------*/

struct FormatAttrNameCase
{
    std::string input;
    std::string expected;
};

class FormatAttrNameTest : public ::testing::TestWithParam<FormatAttrNameCase>
{};

TEST_P(FormatAttrNameTest, formatsCorrectly)
{
    const auto & [input, expected] = GetParam();
    EXPECT_EQ(formatAttrName(input), expected);
}

INSTANTIATE_TEST_SUITE_P(
    FormatAttrName,
    FormatAttrNameTest,
    ::testing::Values(
        // Simple identifier — returned bare
        FormatAttrNameCase{"foo", "foo"},
        // Identifier with underscore
        FormatAttrNameCase{"foo_bar", "foo_bar"},
        // Hyphens are valid in Nix attribute identifiers
        FormatAttrNameCase{"foo-bar", "foo-bar"},
        // Name containing a dot must be quoted
        FormatAttrNameCase{"test.server.example.com", "\"test.server.example.com\""},
        // Name with space must be quoted
        FormatAttrNameCase{"hello world", "\"hello world\""},
        // Empty name needs quoting
        FormatAttrNameCase{"", "\"\""},
        // Name starting with digit is not a valid identifier
        FormatAttrNameCase{"123abc", "\"123abc\""},
        // Reserved keyword "if" needs quoting
        FormatAttrNameCase{"if", "\"if\""},
        // Reserved keyword "let" needs quoting
        FormatAttrNameCase{"let", "\"let\""}));

/* ----------------------------------------------------------------------------
 * matchAttrCompletions (parametrised TEST_P suite)
 * --------------------------------------------------------------------------*/

struct CompletionTestCase
{
    std::string prefix;
    StringSet attrNames;
    StringSet expected;
};

class AttrCompletionTest : public ::testing::TestWithParam<CompletionTestCase>
{};

TEST_P(AttrCompletionTest, matchesExpected)
{
    const auto & [prefix, attrNames, expected] = GetParam();
    auto result = matchAttrCompletions(prefix, attrNames);
    EXPECT_EQ(result, expected);
}

INSTANTIATE_TEST_SUITE_P(
    Completions,
    AttrCompletionTest,
    ::testing::Values(
        // Simple prefix matching after a dot
        CompletionTestCase{"foo.b", {"bar", "baz", "qux"}, {"foo.bar", "foo.baz"}},
        // All attributes match when prefix after dot is empty
        CompletionTestCase{"foo.", {"alpha", "beta"}, {"foo.alpha", "foo.beta"}},
        // No matches when nothing starts with the typed prefix
        CompletionTestCase{"foo.xyz", {"alpha", "beta"}, {}},
        // Exact single match
        CompletionTestCase{"pkg.name", {"name", "version"}, {"pkg.name"}},
        // Attribute name requiring quoting (contains a dot)
        CompletionTestCase{"a.test", {"test.server.example.com", "testing"}, {"a.\"test.server.example.com\"", "a.testing"}},
        // Quoted prefix — user typed opening quote, name needs quoting
        CompletionTestCase{"a.\"test", {"test.server.example.com", "other"}, {"a.\"test.server.example.com\""}},
        // Quoted prefix — user typed opening quote for a plain identifier
        CompletionTestCase{"a.\"foo", {"fooBar", "fooQux"}, {"a.\"fooBar\"", "a.\"fooQux\""}},
        // Attribute name with space requires quoting
        CompletionTestCase{"pkg.he", {"hello world", "help"}, {"pkg.help", "pkg.\"hello world\""}},
        // Attribute name starting with digit requires quoting
        CompletionTestCase{"x.1", {"123abc", "1foo"}, {"x.\"123abc\"", "x.\"1foo\""}},
        // Reserved keyword requires quoting
        CompletionTestCase{"cfg.i", {"if", "import-path"}, {"cfg.\"if\"", "cfg.import-path"}},
        // Multi-level dotted path — only last component is matched
        CompletionTestCase{"a.b.c", {"cat", "car", "dog"}, {"a.b.cat", "a.b.car"}},
        // No unquoted dot — returns empty (not an attr path)
        CompletionTestCase{"nodot", {"foo", "bar"}, {}},
        // Quoted segment in path prefix — dots inside quotes are ignored
        CompletionTestCase{"a.\"b.c\".d", {"dog", "deer", "fox"}, {"a.\"b.c\".dog", "a.\"b.c\".deer"}}));

} // namespace nix
