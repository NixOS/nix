#include "nix/util/closure.hh"
#include <gtest/gtest.h>

namespace nix {

using namespace std;

map<string, set<string>> testGraph = {
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
    set<string> aClosure;
    set<string> expectedClosure = {"A", "B", "C", "F", "G"};
    computeClosure<string>(
        {"A"}, aClosure, [&](const std::string & currentNode) -> asio::awaitable<std::set<std::string>> {
            co_return testGraph[currentNode];
        });

    ASSERT_EQ(aClosure, expectedClosure);
}

TEST(closure, properlyHandlesDirectExceptions)
{
    struct TestExn
    {};

    set<string> aClosure;
    EXPECT_THROW(
        computeClosure<string>(
            {"A"}, aClosure, [&](const std::string &) -> asio::awaitable<std::set<std::string>> { throw TestExn(); }),
        TestExn);
}

} // namespace nix
