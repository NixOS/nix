#include "../args.hh"
#include <list>

#include <gtest/gtest.h>

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

}