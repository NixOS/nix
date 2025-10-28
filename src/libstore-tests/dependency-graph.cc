#include "nix/store/dependency-graph-impl.hh"

#include <gtest/gtest.h>

namespace nix {

TEST(DependencyGraph, BasicAddEdge)
{
    FilePathGraph depGraph;
    depGraph.addEdge("a", "b");
    depGraph.addEdge("b", "c");

    EXPECT_TRUE(depGraph.hasNode("a"));
    EXPECT_TRUE(depGraph.hasNode("b"));
    EXPECT_TRUE(depGraph.hasNode("c"));
    EXPECT_FALSE(depGraph.hasNode("d"));

    // Verify edges using high-level API
    auto successors = depGraph.getSuccessors("a");
    EXPECT_EQ(successors.size(), 1);
    EXPECT_EQ(successors[0], "b");
}

TEST(DependencyGraph, DfsTraversalOrder)
{
    // Build a graph: A->B->D, A->C->D
    // Successors should be visited in distance order (B and C before recursing)
    FilePathGraph depGraph;
    depGraph.addEdge("a", "b");
    depGraph.addEdge("a", "c");
    depGraph.addEdge("b", "d");
    depGraph.addEdge("c", "d");

    std::vector<std::string> visitedNodes;
    std::vector<std::pair<std::string, std::string>> visitedEdges;

    depGraph.dfsFromTarget(
        "a",
        "d",
        [&](const std::string & node, size_t depth) {
            visitedNodes.push_back(node);
            return true;
        },
        [&](const std::string & from, const std::string & to, bool isLast, size_t depth) {
            visitedEdges.emplace_back(from, to);
        },
        [](const std::string &) { return false; });

    EXPECT_EQ(visitedNodes[0], "a");
    // B and C both at distance 1, could be in either order
    EXPECT_TRUE(
        (visitedNodes[1] == "b" && visitedNodes[2] == "d") || (visitedNodes[1] == "c" && visitedNodes[2] == "d"));
}

TEST(DependencyGraph, GetSuccessors)
{
    FilePathGraph depGraph;
    depGraph.addEdge("a", "b");
    depGraph.addEdge("a", "c");

    auto successors = depGraph.getSuccessors("a");
    EXPECT_EQ(successors.size(), 2);
    EXPECT_TRUE(std::ranges::contains(successors, "b"));
    EXPECT_TRUE(std::ranges::contains(successors, "c"));
}

TEST(DependencyGraph, GetAllNodes)
{
    FilePathGraph depGraph;
    depGraph.addEdge("foo", "bar");
    depGraph.addEdge("bar", "baz");

    auto nodes = depGraph.getAllNodes();
    EXPECT_EQ(nodes.size(), 3);
    EXPECT_TRUE(std::ranges::contains(nodes, "foo"));
    EXPECT_TRUE(std::ranges::contains(nodes, "bar"));
    EXPECT_TRUE(std::ranges::contains(nodes, "baz"));
}

TEST(DependencyGraph, ThrowsOnMissingNode)
{
    FilePathGraph depGraph;
    depGraph.addEdge("a", "b");

    EXPECT_THROW((void) depGraph.getSuccessors("nonexistent"), nix::Error);
}

TEST(DependencyGraph, EmptyGraph)
{
    FilePathGraph depGraph;

    EXPECT_FALSE(depGraph.hasNode("anything"));
    EXPECT_EQ(depGraph.numVertices(), 0);
    EXPECT_EQ(depGraph.getAllNodes().size(), 0);
}

} // namespace nix
