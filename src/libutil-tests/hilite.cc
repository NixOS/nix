#include "nix/util/hilite.hh"

#include <gtest/gtest.h>

namespace nix {
/* ----------- tests for fmt.hh -------------------------------------------------*/

TEST(hiliteMatches, noHighlight)
{
    ASSERT_STREQ(hiliteMatches("Hello, world!", std::vector<boost::smatch>(), "(", ")").c_str(), "Hello, world!");
}

TEST(hiliteMatches, simpleHighlight)
{
    std::string str = "Hello, world!";
    boost::regex re = boost::regex("world");
    auto matches = std::vector(boost::sregex_iterator(str.begin(), str.end(), re), boost::sregex_iterator());
    ASSERT_STREQ(hiliteMatches(str, matches, "(", ")").c_str(), "Hello, (world)!");
}

TEST(hiliteMatches, multipleMatches)
{
    std::string str = "Hello, world, world, world, world, world, world, Hello!";
    boost::regex re = boost::regex("world");
    auto matches = std::vector(boost::sregex_iterator(str.begin(), str.end(), re), boost::sregex_iterator());
    ASSERT_STREQ(
        hiliteMatches(str, matches, "(", ")").c_str(),
        "Hello, (world), (world), (world), (world), (world), (world), Hello!");
}

TEST(hiliteMatches, overlappingMatches)
{
    std::string str = "world, Hello, world, Hello, world, Hello, world, Hello, world!";
    boost::regex re = boost::regex("Hello, world");
    boost::regex re2 = boost::regex("world, Hello");
    auto v = std::vector(boost::sregex_iterator(str.begin(), str.end(), re), boost::sregex_iterator());
    for (auto it = boost::sregex_iterator(str.begin(), str.end(), re2); it != boost::sregex_iterator(); ++it) {
        v.push_back(*it);
    }
    ASSERT_STREQ(
        hiliteMatches(str, v, "(", ")").c_str(), "(world, Hello, world, Hello, world, Hello, world, Hello, world)!");
}

TEST(hiliteMatches, complexOverlappingMatches)
{
    std::string str = "legacyPackages.x86_64-linux.git-crypt";
    std::vector regexes = {
        boost::regex("t-cry"),
        boost::regex("ux\\.git-cry"),
        boost::regex("git-c"),
        boost::regex("pt"),
    };
    std::vector<boost::smatch> matches;
    for (const auto & regex : regexes) {
        for (auto it = boost::sregex_iterator(str.begin(), str.end(), regex); it != boost::sregex_iterator(); ++it) {
            matches.push_back(*it);
        }
    }
    ASSERT_STREQ(hiliteMatches(str, matches, "(", ")").c_str(), "legacyPackages.x86_64-lin(ux.git-crypt)");
}
} // namespace nix
