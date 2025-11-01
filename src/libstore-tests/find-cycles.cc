#include "nix/store/build/find-cycles.hh"

#include <gtest/gtest.h>
#include <algorithm>

namespace nix {

/**
 * Parameters for transformEdgesToMultiedges tests
 */
struct TransformEdgesParams
{
    std::string description;
    std::vector<std::vector<std::string>> inputEdges;
    std::vector<std::vector<std::string>> expectedOutput;

    friend std::ostream & operator<<(std::ostream & os, const TransformEdgesParams & params)
    {
        os << "Test: " << params.description << "\n";
        os << "Input edges (" << params.inputEdges.size() << "):\n";
        for (const auto & edge : params.inputEdges) {
            os << "  ";
            for (size_t i = 0; i < edge.size(); ++i) {
                if (i > 0)
                    os << " -> ";
                os << edge[i];
            }
            os << "\n";
        }
        os << "Expected output (" << params.expectedOutput.size() << "):\n";
        for (const auto & multiedge : params.expectedOutput) {
            os << "  ";
            for (size_t i = 0; i < multiedge.size(); ++i) {
                if (i > 0)
                    os << " -> ";
                os << multiedge[i];
            }
            os << "\n";
        }
        return os;
    }
};

class TransformEdgesToMultiedgesTest : public ::testing::TestWithParam<TransformEdgesParams>
{};

namespace {
// Helper to convert vector<vector<string>> to StoreCycleEdgeVec
StoreCycleEdgeVec toStoreCycleEdgeVec(const std::vector<std::vector<std::string>> & edges)
{
    StoreCycleEdgeVec result;
    result.reserve(edges.size());
    for (const auto & edge : edges) {
        result.emplace_back(edge.begin(), edge.end());
    }
    return result;
}

// Comparator for sorting multiedges deterministically
bool compareMultiedges(const StoreCycleEdge & a, const StoreCycleEdge & b)
{
    if (a.size() != b.size())
        return a.size() < b.size();
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
}
} // namespace

TEST_P(TransformEdgesToMultiedgesTest, TransformEdges)
{
    const auto & params = GetParam();

    auto inputEdges = toStoreCycleEdgeVec(params.inputEdges);
    StoreCycleEdgeVec actualOutput;
    transformEdgesToMultiedges(inputEdges, actualOutput);

    auto expectedOutput = toStoreCycleEdgeVec(params.expectedOutput);

    ASSERT_EQ(actualOutput.size(), expectedOutput.size()) << "Number of multiedges doesn't match expected";

    // Sort both for comparison (order may vary, but content should match)
    std::sort(actualOutput.begin(), actualOutput.end(), compareMultiedges);
    std::sort(expectedOutput.begin(), expectedOutput.end(), compareMultiedges);

    // Compare each multiedge
    EXPECT_EQ(actualOutput, expectedOutput);
}

INSTANTIATE_TEST_CASE_P(
    FindCycles,
    TransformEdgesToMultiedgesTest,
    ::testing::Values(
        // Empty input
        TransformEdgesParams{"empty input", {}, {}},

        // Single edge - no joining possible
        TransformEdgesParams{"single edge", {{"a", "b"}}, {{"a", "b"}}},

        // Two edges that connect (append case: A->B, B->C becomes A->B->C)
        TransformEdgesParams{"two edges connecting via append", {{"a", "b"}, {"b", "c"}}, {{"a", "b", "c"}}},

        // Two edges that connect (prepend case: B->C, A->B becomes A->B->C)
        TransformEdgesParams{"two edges connecting via prepend", {{"b", "c"}, {"a", "b"}}, {{"a", "b", "c"}}},

        // Complete cycle (A->B, B->C, C->A becomes A->B->C->A)
        TransformEdgesParams{"complete cycle", {{"a", "b"}, {"b", "c"}, {"c", "a"}}, {{"a", "b", "c", "a"}}},

        // Two disjoint edges - no joining
        TransformEdgesParams{"disjoint edges", {{"a", "b"}, {"c", "d"}}, {{"a", "b"}, {"c", "d"}}},

        // Chain of multiple edges (A->B, B->C, C->D, D->E)
        TransformEdgesParams{
            "chain of edges", {{"a", "b"}, {"b", "c"}, {"c", "d"}, {"d", "e"}}, {{"a", "b", "c", "d", "e"}}},

        // Multiple disjoint cycles
        TransformEdgesParams{
            "multiple disjoint cycles",
            {{"a", "b"}, {"b", "a"}, {"c", "d"}, {"d", "c"}},
            {{"a", "b", "a"}, {"c", "d", "c"}}},

        // Complex graph requiring multiple merge passes
        // First pass: (A->B, B->C) -> (A->B->C)
        // Then: (A->B->C, C->D) -> (A->B->C->D)
        // Then: (D->A, A->B->C->D) -> (A->B->C->D->A)
        TransformEdgesParams{
            "complex requiring multiple passes",
            {{"a", "b"}, {"b", "c"}, {"c", "d"}, {"d", "a"}},
            {{"a", "b", "c", "d", "a"}}},

        // Y-shaped graph (A->B, B->C, B->D)
        // B->C and B->D can't connect, but A->B can prepend to B->C and A->B can prepend to B->D
        // However, once A->B joins with B->C to form A->B->C, the original A->B is consumed
        // So we should get A->B->C and B->D (or A->B->D and B->C depending on order)
        TransformEdgesParams{"Y-shaped graph", {{"a", "b"}, {"b", "c"}, {"b", "d"}}, {{"a", "b", "c"}, {"b", "d"}}},

        // Edge with longer path (multi-hop edge)
        TransformEdgesParams{
            "edge with multiple hops", {{"a", "x", "y", "b"}, {"b", "c"}}, {{"a", "x", "y", "b", "c"}}},

        // Self-loop edge
        TransformEdgesParams{"self-loop", {{"a", "a"}}, {{"a", "a"}}},

        // Reverse order joining (tests prepend logic thoroughly)
        TransformEdgesParams{
            "reverse order joining", {{"d", "e"}, {"c", "d"}, {"b", "c"}, {"a", "b"}}, {{"a", "b", "c", "d", "e"}}}));

} // namespace nix
