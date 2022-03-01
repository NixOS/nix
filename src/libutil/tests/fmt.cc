#include "fmt.hh"

#include <gtest/gtest.h>

#include <regex>

namespace nix {
/* ----------- tests for fmt.hh -------------------------------------------------*/

    TEST(hiliteMatches, noHighlight) {
        ASSERT_STREQ(hiliteMatches("Hello, world!", std::vector<std::smatch>(), "(", ")").c_str(), "Hello, world!");
    }

    TEST(hiliteMatches, simpleHighlight) {
        std::string str = "Hello, world!";
        std::regex re = std::regex("world");
        auto matches = std::vector(std::sregex_iterator(str.begin(), str.end(), re), std::sregex_iterator());
        ASSERT_STREQ(
                    hiliteMatches(str, matches, "(", ")").c_str(),
                    "Hello, (world)!"
        );
    }

    TEST(hiliteMatches, multipleMatches) {
        std::string str = "Hello, world, world, world, world, world, world, Hello!";
        std::regex re = std::regex("world");
        auto matches = std::vector(std::sregex_iterator(str.begin(), str.end(), re), std::sregex_iterator());
        ASSERT_STREQ(
                    hiliteMatches(str, matches, "(", ")").c_str(),
                    "Hello, (world), (world), (world), (world), (world), (world), Hello!"
        );
    }

    TEST(hiliteMatches, overlappingMatches) {
        std::string str = "world, Hello, world, Hello, world, Hello, world, Hello, world!";
        std::regex re = std::regex("Hello, world");
        std::regex re2 = std::regex("world, Hello");
        auto v = std::vector(std::sregex_iterator(str.begin(), str.end(), re), std::sregex_iterator());
        for(auto it = std::sregex_iterator(str.begin(), str.end(), re2); it != std::sregex_iterator(); ++it) {
            v.push_back(*it);
        }
        ASSERT_STREQ(
                    hiliteMatches(str, v, "(", ")").c_str(),
                    "(world, Hello, world, Hello, world, Hello, world, Hello, world)!"
        );
    }

    TEST(hiliteMatches, complexOverlappingMatches) {
        std::string str = "legacyPackages.x86_64-linux.git-crypt";
        std::vector regexes = {
            std::regex("t-cry"),
            std::regex("ux\\.git-cry"),
            std::regex("git-c"),
            std::regex("pt"),
        };
        std::vector<std::smatch> matches;
        for(auto regex : regexes)
        {
            for(auto it = std::sregex_iterator(str.begin(), str.end(), regex); it != std::sregex_iterator(); ++it) {
                matches.push_back(*it);
            }
        }
        ASSERT_STREQ(
                    hiliteMatches(str, matches, "(", ")").c_str(),
                    "legacyPackages.x86_64-lin(ux.git-crypt)"
        );
    }
}
