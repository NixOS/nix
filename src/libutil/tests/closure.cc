#include "closure.hh"
#include <gtest/gtest.h>

namespace nix {

using namespace std;

map<string, set<string>> testGraph = {
    { "A", { "B", "C", "G" } },
    { "B", { "A" } }, // Loops back to A
    { "C", { "F" } }, // Indirect reference
    { "D", { "A" } }, // Not reachable, but has backreferences
    { "E", {} }, // Just not reachable
    { "F", {} },
    { "G", { "G" } }, // Self reference
};

TEST(closure, correctClosure) {
    set<string> aClosure;
    set<string> expectedClosure = {"A", "B", "C", "F", "G"};
    computeClosure<string>(
        {"A"},
        aClosure,
        [&](const string currentNode, function<void(promise<set<string>> &)> processEdges) {
            promise<set<string>> promisedNodes;
            promisedNodes.set_value(testGraph[currentNode]);
            processEdges(promisedNodes);
        }
    );

    ASSERT_EQ(aClosure, expectedClosure);
}

TEST(closure, properlyHandlesDirectExceptions) {
    struct TestExn {};
    set<string> aClosure;
    EXPECT_THROW(
        computeClosure<string>(
            {"A"},
            aClosure,
            [&](const string currentNode, function<void(promise<set<string>> &)> processEdges) {
                throw TestExn();
            }
        ),
        TestExn
    );
}

TEST(closure, properlyHandlesExceptionsInPromise) {
    struct TestExn {};
    set<string> aClosure;
    EXPECT_THROW(
        computeClosure<string>(
            {"A"},
            aClosure,
            [&](const string currentNode, function<void(promise<set<string>> &)> processEdges) {
                promise<set<string>> promise;
                try {
                    throw TestExn();
                } catch (...) {
                    promise.set_exception(std::current_exception());
                }
                processEdges(promise);
            }
        ),
        TestExn
    );
}

}
