#pragma once
///@file

#include "nix/store/path.hh"
#include "nix/util/canon-path.hh"

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/dijkstra_shortest_paths.hpp>
#include <boost/graph/reverse_graph.hpp>

#include <map>
#include <vector>
#include <optional>
#include <concepts>

namespace nix {

class Store;

/**
 * Concept for types usable as graph node IDs.
 */
template<typename T>
concept GraphNodeId = std::copyable<T> && std::totally_ordered<T>;

/**
 * Directed graph for dependency analysis using Boost Graph Library.
 *
 * @tparam NodeId Node identifier type (e.g., StorePath, std::string)
 * @tparam EdgeProperty Optional edge metadata type
 */
template<GraphNodeId NodeId, typename EdgeProperty = boost::no_property>
class DependencyGraph
{
public:
    /**
     * Bundled vertex property. Uses optional for default constructibility.
     */
    struct VertexProperty
    {
        std::optional<NodeId> id;
    };

    /**
     * BGL adjacency_list: bidirectional, vector storage.
     */
    using Graph = boost::adjacency_list<boost::vecS, boost::vecS, boost::bidirectionalS, VertexProperty, EdgeProperty>;

    using vertex_descriptor = typename boost::graph_traits<Graph>::vertex_descriptor;
    using edge_descriptor = typename boost::graph_traits<Graph>::edge_descriptor;

private:
    Graph graph;
    std::map<NodeId, vertex_descriptor> nodeToVertex;

    // Cached algorithm results
    mutable std::optional<std::vector<size_t>> cachedDistances;
    mutable std::optional<NodeId> distanceTarget;

    // Internal helpers
    vertex_descriptor addOrGetVertex(const NodeId & id);
    std::optional<vertex_descriptor> getVertex(const NodeId & id) const;
    const NodeId & getNodeId(vertex_descriptor v) const;
    vertex_descriptor getVertexOrThrow(const NodeId & id) const;
    void computeDistancesFrom(const NodeId & target) const;

public:
    DependencyGraph() = default;

    /**
     * Build graph from Store closure (StorePath graphs only).
     *
     * @param store Store to query for references
     * @param closure Store paths to include
     */
    DependencyGraph(Store & store, const StorePathSet & closure)
        requires std::same_as<NodeId, StorePath>;

    /**
     * Add edge, creating vertices if needed.
     */
    void addEdge(const NodeId & from, const NodeId & to);

    /**
     * Add edge with property. Merges property if edge exists.
     */
    void addEdge(const NodeId & from, const NodeId & to, const EdgeProperty & prop)
        requires(!std::same_as<EdgeProperty, boost::no_property>);

    bool hasNode(const NodeId & id) const;

    /**
     * DFS traversal with distance-based successor ordering.
     * Successors visited in order of increasing distance to target.
     * Automatically computes distances if needed (lazy).
     *
     * Example traversal from A to D:
     *
     *     A (dist=3)
     *     ├─→ B (dist=2)
     *     │   └─→ D (dist=0) [target]
     *     └─→ C (dist=2)
     *         └─→ D (dist=0)
     *
     * Callbacks invoked:
     *   visitNode(A, depth=0) -> true
     *   visitEdge(A, B, isLast=false, depth=0)
     *   visitNode(B, depth=1) -> true
     *   visitEdge(B, D, isLast=true, depth=1)
     *   visitNode(D, depth=2) -> true
     *   shouldStop(D) -> true [stops traversal]
     *
     * @param start Starting node for traversal
     * @param target Target node (used for distance-based sorting)
     * @param visitNode Called when entering node: (node, depth) -> bool. Return false to skip subtree.
     * @param visitEdge Called for each edge: (from, to, isLastEdge, depth) -> void
     * @param shouldStop Called after visiting node: (node) -> bool. Return true to stop entire traversal.
     */
    template<typename NodeVisitor, typename EdgeVisitor, typename StopPredicate>
    void dfsFromTarget(
        const NodeId & start,
        const NodeId & target,
        NodeVisitor && visitNode,
        EdgeVisitor && visitEdge,
        StopPredicate && shouldStop) const;

    /**
     * Get successor nodes (outgoing edges).
     */
    std::vector<NodeId> getSuccessors(const NodeId & node) const;

    /**
     * Get edge property. Returns nullopt if edge doesn't exist.
     */
    std::optional<EdgeProperty> getEdgeProperty(const NodeId & from, const NodeId & to) const
        requires(!std::same_as<EdgeProperty, boost::no_property>);

    std::vector<NodeId> getAllNodes() const;

    size_t numVertices() const
    {
        return boost::num_vertices(graph);
    }
};

/**
 * Edge property storing which files created a dependency.
 */
struct FileListEdgeProperty
{
    std::vector<CanonPath> files;
};

// Convenience typedefs
using StorePathGraph = DependencyGraph<StorePath>;
using FilePathGraph = DependencyGraph<std::string>;
using StorePathGraphWithFiles = DependencyGraph<StorePath, FileListEdgeProperty>;

} // namespace nix
