#include "args.hh"
#include "fs-sink.hh"
#include <list>

#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

namespace nix {

    TEST(parseShebangContent, basic) {
        std::list<std::string> r = parseShebangContent("hi there");
        ASSERT_EQ(r.size(), 2);
        auto i = r.begin();
        ASSERT_EQ(*i++, "hi");
        ASSERT_EQ(*i++, "there");
    }

    TEST(parseShebangContent, empty) {
        std::list<std::string> r = parseShebangContent("");
        ASSERT_EQ(r.size(), 0);
    }

    TEST(parseShebangContent, doubleBacktick) {
        std::list<std::string> r = parseShebangContent("``\"ain't that nice\"``");
        ASSERT_EQ(r.size(), 1);
        auto i = r.begin();
        ASSERT_EQ(*i++, "\"ain't that nice\"");
    }

    TEST(parseShebangContent, doubleBacktickEmpty) {
        std::list<std::string> r = parseShebangContent("````");
        ASSERT_EQ(r.size(), 1);
        auto i = r.begin();
        ASSERT_EQ(*i++, "");
    }

    TEST(parseShebangContent, doubleBacktickMarkdownInlineCode) {
        std::list<std::string> r = parseShebangContent("``# I'm markdown section about `coolFunction` ``");
        ASSERT_EQ(r.size(), 1);
        auto i = r.begin();
        ASSERT_EQ(*i++, "# I'm markdown section about `coolFunction`");
    }

    TEST(parseShebangContent, doubleBacktickMarkdownCodeBlockNaive) {
        std::list<std::string> r = parseShebangContent("``Example 1\n```nix\na: a\n``` ``");
        auto i = r.begin();
        ASSERT_EQ(r.size(), 1);
        ASSERT_EQ(*i++, "Example 1\n``nix\na: a\n``");
    }

    TEST(parseShebangContent, doubleBacktickMarkdownCodeBlockCorrect) {
        std::list<std::string> r = parseShebangContent("``Example 1\n````nix\na: a\n```` ``");
        auto i = r.begin();
        ASSERT_EQ(r.size(), 1);
        ASSERT_EQ(*i++, "Example 1\n```nix\na: a\n```");
    }

    TEST(parseShebangContent, doubleBacktickMarkdownCodeBlock2) {
        std::list<std::string> r = parseShebangContent("``Example 1\n````nix\na: a\n````\nExample 2\n````nix\na: a\n```` ``");
        auto i = r.begin();
        ASSERT_EQ(r.size(), 1);
        ASSERT_EQ(*i++, "Example 1\n```nix\na: a\n```\nExample 2\n```nix\na: a\n```");
    }

    TEST(parseShebangContent, singleBacktickInDoubleBacktickQuotes) {
        std::list<std::string> r = parseShebangContent("``` ``");
        auto i = r.begin();
        ASSERT_EQ(r.size(), 1);
        ASSERT_EQ(*i++, "`");
    }

    TEST(parseShebangContent, singleBacktickAndSpaceInDoubleBacktickQuotes) {
        std::list<std::string> r = parseShebangContent("```  ``");
        auto i = r.begin();
        ASSERT_EQ(r.size(), 1);
        ASSERT_EQ(*i++, "` ");
    }

    TEST(parseShebangContent, doubleBacktickInDoubleBacktickQuotes) {
        std::list<std::string> r = parseShebangContent("````` ``");
        auto i = r.begin();
        ASSERT_EQ(r.size(), 1);
        ASSERT_EQ(*i++, "``");
    }

    TEST(parseShebangContent, increasingQuotes) {
        std::list<std::string> r = parseShebangContent("```` ``` `` ````` `` `````` ``");
        auto i = r.begin();
        ASSERT_EQ(r.size(), 4);
        ASSERT_EQ(*i++, "");
        ASSERT_EQ(*i++, "`");
        ASSERT_EQ(*i++, "``");
        ASSERT_EQ(*i++, "```");
    }


#ifndef COVERAGE

// quick and dirty
static inline std::string escape(std::string_view s_) {

    std::string_view s = s_;
    std::string r = "``";

    // make a guess to allocate ahead of time
    r.reserve(
        // plain chars
        s.size()
        // quotes
        + 5
        // some "escape" backticks
        + s.size() / 8);

    while (!s.empty()) {
        if (s[0] == '`' && s.size() >= 2 && s[1] == '`') {
            // escape it
            r += "`";
            while (!s.empty() && s[0] == '`') {
                r += "`";
                s = s.substr(1);
            }
        } else {
            r += s[0];
            s = s.substr(1);
        }
    }

    if (!r.empty()
        && (
            r[r.size() - 1] == '`'
            || r[r.size() - 1] == ' '
        )) {
        r += " ";
    }

    r += "``";

    return r;
};

RC_GTEST_PROP(
    parseShebangContent,
    prop_round_trip_single,
    (const std::string & orig))
{
    auto escaped = escape(orig);
    // RC_LOG() << "escaped: <[[" << escaped << "]]>" << std::endl;
    auto ss = parseShebangContent(escaped);
    RC_ASSERT(ss.size() == 1);
    RC_ASSERT(*ss.begin() == orig);
}

RC_GTEST_PROP(
    parseShebangContent,
    prop_round_trip_two,
    (const std::string & one, const std::string & two))
{
    auto ss = parseShebangContent(escape(one) + " " + escape(two));
    RC_ASSERT(ss.size() == 2);
    auto i = ss.begin();
    RC_ASSERT(*i++ == one);
    RC_ASSERT(*i++ == two);
}


#endif

}
