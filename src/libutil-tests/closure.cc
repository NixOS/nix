#include "nix/util/closure.hh"
#include <gtest/gtest.h>

namespace nix {

std::map<std::string, std::set<std::string>> testGraph = {
    {"A", {"B", "C", "G"}},
    {"B", {"A"}}, // Loops back to A
    {"C", {"F"}}, // Indirect reference
    {"D", {"A"}}, // Not reachable, but has backreferences
    {"E", {}},    // Just not reachable
    {"F", {}},
    {"G", {"G"}}, // Self reference
};

TEST(closure, correctClosure)
{
    std::set<std::string> aClosure;
    std::set<std::string> expectedClosure = {"A", "B", "C", "F", "G"};
    computeClosure<std::string>(
        {"A"}, aClosure, [&](const std::string & currentNode) -> asio::awaitable<std::set<std::string>> {
            co_return testGraph[currentNode];
        });

    ASSERT_EQ(aClosure, expectedClosure);
}

TEST(closure, properlyHandlesDirectExceptions)
{
    struct TestExn
    {};

    std::set<std::string> aClosure;
    std::size_t callCount = 0;
    EXPECT_THROW(
        computeClosure<std::string>(
            {"A", "B"},
            aClosure,
            [&](const std::string &) -> asio::awaitable<std::set<std::string>> {
                if (callCount++ == 0)
                    throw TestExn();
                co_return std::set<std::string>{};
            }),
        TestExn);
    ASSERT_EQ(callCount, 2);
}

} // namespace nix
