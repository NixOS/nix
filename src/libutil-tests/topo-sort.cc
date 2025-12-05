#include <map>
#include <set>
#include <string>
#include <vector>
#include <algorithm>

#include <gtest/gtest.h>

#include "nix/util/topo-sort.hh"
#include "nix/util/util.hh"

namespace nix {

/**
 * Helper function to create a graph and run topoSort
 */
TopoSortResult<std::string>
runTopoSort(const std::set<std::string> & nodes, const std::map<std::string, std::set<std::string>> & edges)
{
    return topoSort(
        nodes,
        std::function<std::set<std::string>(const std::string &)>(
            [&](const std::string & node) -> std::set<std::string> {
                auto it = edges.find(node);
                return it != edges.end() ? it->second : std::set<std::string>{};
            }));
}

/**
 * Helper to check if a sorted result respects dependencies
 *
 * @note `topoSort` returns results in REVERSE topological order (see
 * line 61 of topo-sort.hh). This means dependents come BEFORE their
 * dependencies in the output.
 *
 * In the edges std::map, if parent -> child, it means parent depends on
 * child, so parent must come BEFORE child in the output from topoSort.
 */
bool isValidTopologicalOrder(
    const std::vector<std::string> & sorted, const std::map<std::string, std::set<std::string>> & edges)
{
    std::map<std::string, size_t> position;
    for (size_t i = 0; i < sorted.size(); ++i) {
        position[sorted[i]] = i;
    }

    // For each edge parent -> children, parent depends on children
    // topoSort reverses the output, so parent comes BEFORE children
    for (const auto & [parent, children] : edges) {
        for (const auto & child : children) {
            if (position.count(parent) && position.count(child)) {
                // parent should come before child (have a smaller index)
                if (position[parent] > position[child]) {
                    return false;
                }
            }
        }
    }
    return true;
}

// ============================================================================
// Parametrized Tests for Topological Sort
// ============================================================================

struct ExpectSuccess
{
    std::optional<std::vector<std::string>> order; // std::nullopt = any valid order is acceptable
};

struct ExpectCycle
{
    std::set<std::string> involvedNodes;
};

using ExpectedResult = std::variant<ExpectSuccess, ExpectCycle>;

struct TopoSortCase
{
    std::string name;
    std::set<std::string> nodes;
    std::map<std::string, std::set<std::string>> edges;
    ExpectedResult expected;
};

class TopoSortTest : public ::testing::TestWithParam<TopoSortCase>
{};

TEST_P(TopoSortTest, ProducesCorrectResult)
{
    const auto & testCase = GetParam();
    auto result = runTopoSort(testCase.nodes, testCase.edges);

    std::visit(
        overloaded{
            [&](const ExpectSuccess & expect) {
                // Success case
                ASSERT_TRUE(holds_alternative<std::vector<std::string>>(result))
                    << "Expected successful sort for: " << testCase.name;

                auto sorted = get<std::vector<std::string>>(result);
                ASSERT_EQ(sorted.size(), testCase.nodes.size())
                    << "Sorted output should contain all nodes for: " << testCase.name;

                ASSERT_TRUE(isValidTopologicalOrder(sorted, testCase.edges))
                    << "Invalid topological order for: " << testCase.name;

                if (expect.order) {
                    ASSERT_EQ(sorted, *expect.order) << "Expected specific order for: " << testCase.name;
                }
            },
            [&](const ExpectCycle & expect) {
                // Cycle detection case
                ASSERT_TRUE(holds_alternative<Cycle<std::string>>(result))
                    << "Expected cycle detection for: " << testCase.name;

                auto cycle = get<Cycle<std::string>>(result);

                // Verify that the cycle involves expected nodes
                ASSERT_TRUE(expect.involvedNodes.count(cycle.path) > 0)
                    << "Cycle path '" << cycle.path << "' not in expected cycle nodes for: " << testCase.name;
                ASSERT_TRUE(expect.involvedNodes.count(cycle.parent) > 0)
                    << "Cycle parent '" << cycle.parent << "' not in expected cycle nodes for: " << testCase.name;

                // Verify that there's actually an edge in the cycle
                auto it = testCase.edges.find(cycle.parent);
                ASSERT_TRUE(it != testCase.edges.end()) << "Parent node should have edges for: " << testCase.name;
                ASSERT_TRUE(it->second.count(cycle.path) > 0)
                    << "Should be an edge from parent to path for: " << testCase.name;
            }},
        testCase.expected);
}

INSTANTIATE_TEST_SUITE_P(
    TopoSort,
    TopoSortTest,
    ::testing::Values(
        // Success cases
        TopoSortCase{
            .name = "EmptySet",
            .nodes = {},
            .edges = {},
            .expected = ExpectSuccess{.order = std::vector<std::string>{}},
        },
        TopoSortCase{
            .name = "SingleNode",
            .nodes = {"A"},
            .edges = {},
            .expected = ExpectSuccess{.order = std::vector<std::string>{"A"}},
        },
        TopoSortCase{
            .name = "TwoIndependentNodes",
            .nodes = {"A", "B"},
            .edges = {},
            // Order between independent nodes is unspecified
            .expected = ExpectSuccess{.order = std::nullopt},
        },
        TopoSortCase{
            .name = "SimpleChain",
            .nodes = {"A", "B", "C"},
            .edges{
                {"A", {"B"}},
                {"B", {"C"}},
            },
            .expected = ExpectSuccess{.order = std::vector<std::string>{"A", "B", "C"}},
        },
        TopoSortCase{
            .name = "SimpleDag",
            .nodes = {"A", "B", "C", "D"},
            .edges{
                {"A", {"B", "C"}},
                {"B", {"D"}},
                {"C", {"D"}},
            },
            .expected = ExpectSuccess{.order = std::nullopt},
        },
        TopoSortCase{
            .name = "DiamondDependency",
            .nodes = {"A", "B", "C", "D"},
            .edges{
                {"A", {"B", "C"}},
                {"B", {"D"}},
                {"C", {"D"}},
            },
            .expected = ExpectSuccess{.order = std::nullopt},
        },
        TopoSortCase{
            .name = "DisconnectedComponents",
            .nodes = {"A", "B", "C", "D"},
            .edges{
                {"A", {"B"}},
                {"C", {"D"}},
            },
            .expected = ExpectSuccess{.order = std::nullopt},
        },
        TopoSortCase{
            .name = "NodeWithNoReferences",
            .nodes = {"A", "B", "C"},
            .edges{
                {"A", {"B"}},
                // C has no dependencies
            },
            .expected = ExpectSuccess{.order = std::nullopt},
        },
        TopoSortCase{
            .name = "MissingReferences",
            .nodes = {"A", "B"},
            .edges{
                // Z doesn't exist in nodes, should be ignored
                {"A", {"B", "Z"}},
            },
            .expected = ExpectSuccess{.order = std::vector<std::string>{"A", "B"}},
        },
        TopoSortCase{
            .name = "ComplexDag",
            .nodes = {"A", "B", "C", "D", "E", "F", "G", "H"},
            .edges{
                {"A", {"B", "C", "D"}},
                {"B", {"E", "F"}},
                {"C", {"E", "F"}},
                {"D", {"G"}},
                {"E", {"H"}},
                {"F", {"H"}},
                {"G", {"H"}},
            },
            .expected = ExpectSuccess{.order = std::nullopt},
        },
        TopoSortCase{
            .name = "LongChain",
            .nodes = {"A", "B", "C", "D", "E", "F", "G", "H"},
            .edges{
                {"A", {"B"}},
                {"B", {"C"}},
                {"C", {"D"}},
                {"D", {"E"}},
                {"E", {"F"}},
                {"F", {"G"}},
                {"G", {"H"}},
            },
            .expected = ExpectSuccess{.order = std::vector<std::string>{"A", "B", "C", "D", "E", "F", "G", "H"}},
        },
        TopoSortCase{
            .name = "SelfLoopIgnored",
            .nodes = {"A"},
            .edges{
                // Self-reference should be ignored per line 41 of topo-sort.hh
                {"A", {"A"}},
            },
            .expected = ExpectSuccess{.order = std::vector<std::string>{"A"}},
        },
        TopoSortCase{
            .name = "SelfLoopInChainIgnored",
            .nodes = {"A", "B", "C"},
            .edges{
                // B has self-reference that should be ignored
                {"A", {"B"}},
                {"B", {"B", "C"}},
            },
            .expected = ExpectSuccess{.order = std::vector<std::string>{"A", "B", "C"}},
        },
        // Cycle detection cases
        TopoSortCase{
            .name = "TwoNodeCycle",
            .nodes = {"A", "B"},
            .edges{
                {"A", {"B"}},
                {"B", {"A"}},
            },
            .expected = ExpectCycle{.involvedNodes = {"A", "B"}},
        },
        TopoSortCase{
            .name = "ThreeNodeCycle",
            .nodes = {"A", "B", "C"},
            .edges{
                {"A", {"B"}},
                {"B", {"C"}},
                {"C", {"A"}},
            },
            .expected = ExpectCycle{.involvedNodes = {"A", "B", "C"}},
        },
        TopoSortCase{
            .name = "CycleInLargerGraph",
            .nodes = {"A", "B", "C", "D"},
            .edges{
                {"A", {"B"}},
                {"B", {"C"}},
                {"C", {"A"}},
                {"D", {"A"}},
            },
            .expected = ExpectCycle{.involvedNodes = {"A", "B", "C"}},
        },
        TopoSortCase{
            .name = "MultipleCycles",
            .nodes = {"A", "B", "C", "D"},
            .edges{
                {"A", {"B"}},
                {"B", {"A"}},
                {"C", {"D"}},
                {"D", {"C"}},
            },
            // Either cycle is valid
            .expected = ExpectCycle{.involvedNodes = {"A", "B", "C", "D"}},
        },
        TopoSortCase{
            .name = "ComplexCycleWithBranches",
            .nodes = {"A", "B", "C", "D", "E"},
            .edges{
                // Cycle: B -> D -> E -> B
                {"A", {"B", "C"}},
                {"B", {"D"}},
                {"C", {"D"}},
                {"D", {"E"}},
                {"E", {"B"}},
            },
            .expected = ExpectCycle{.involvedNodes = {"B", "D", "E"}},
        }));

} // namespace nix
