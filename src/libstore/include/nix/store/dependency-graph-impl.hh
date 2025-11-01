#pragma once
/**
 * @file
 *
 * Template implementations (as opposed to mere declarations).
 *
 * This file is an example of the "impl.hh" pattern. See the
 * contributing guide.
 *
 * One only needs to include this when instantiating DependencyGraph
 * with custom NodeId or EdgeProperty types beyond the pre-instantiated
 * common types (StorePath, std::string).
 */

#include "nix/store/dependency-graph.hh"
#include "nix/store/store-api.hh"
#include "nix/util/error.hh"

#include <boost/graph/graph_traits.hpp>
#include <boost/graph/depth_first_search.hpp>
#include <boost/graph/reverse_graph.hpp>
#include <boost/graph/properties.hpp>

#include <algorithm>
#include <ranges>

namespace nix {

template<GraphNodeId NodeId, typename EdgeProperty>
DependencyGraph<NodeId, EdgeProperty>::DependencyGraph(Store & store, const StorePathSet & closure)
    requires std::same_as<NodeId, StorePath>
{
    for (auto & path : closure) {
        for (auto & ref : store.queryPathInfo(path)->references) {
            addEdge(path, ref);
        }
    }
}

template<GraphNodeId NodeId, typename EdgeProperty>
typename DependencyGraph<NodeId, EdgeProperty>::vertex_descriptor
DependencyGraph<NodeId, EdgeProperty>::addOrGetVertex(const NodeId & id)
{
    auto it = nodeToVertex.find(id);
    if (it != nodeToVertex.end()) {
        return it->second;
    }

    auto v = boost::add_vertex(VertexProperty{std::make_optional(id)}, graph);
    nodeToVertex[id] = v;
    return v;
}

template<GraphNodeId NodeId, typename EdgeProperty>
void DependencyGraph<NodeId, EdgeProperty>::addEdge(const NodeId & from, const NodeId & to)
{
    auto vFrom = addOrGetVertex(from);
    auto vTo = addOrGetVertex(to);
    boost::add_edge(vFrom, vTo, graph);
}

template<GraphNodeId NodeId, typename EdgeProperty>
void DependencyGraph<NodeId, EdgeProperty>::addEdge(const NodeId & from, const NodeId & to, const EdgeProperty & prop)
    requires(!std::same_as<EdgeProperty, boost::no_property>)
{
    auto vFrom = addOrGetVertex(from);
    auto vTo = addOrGetVertex(to);

    auto [existingEdge, found] = boost::edge(vFrom, vTo, graph);
    if (found) {
        if constexpr (std::same_as<EdgeProperty, FileListEdgeProperty>) {
            auto & edgeFiles = graph[existingEdge].files;
            edgeFiles.insert(edgeFiles.end(), prop.files.begin(), prop.files.end());
        }
    } else {
        boost::add_edge(vFrom, vTo, prop, graph);
    }
}

template<GraphNodeId NodeId, typename EdgeProperty>
std::optional<typename DependencyGraph<NodeId, EdgeProperty>::vertex_descriptor>
DependencyGraph<NodeId, EdgeProperty>::getVertex(const NodeId & id) const
{
    auto it = nodeToVertex.find(id);
    if (it == nodeToVertex.end()) {
        return std::nullopt;
    }
    return it->second;
}

template<GraphNodeId NodeId, typename EdgeProperty>
const NodeId & DependencyGraph<NodeId, EdgeProperty>::getNodeId(vertex_descriptor v) const
{
    return *graph[v].id;
}

template<GraphNodeId NodeId, typename EdgeProperty>
bool DependencyGraph<NodeId, EdgeProperty>::hasNode(const NodeId & id) const
{
    return nodeToVertex.contains(id);
}

template<GraphNodeId NodeId, typename EdgeProperty>
typename DependencyGraph<NodeId, EdgeProperty>::vertex_descriptor
DependencyGraph<NodeId, EdgeProperty>::getVertexOrThrow(const NodeId & id) const
{
    auto opt = getVertex(id);
    if (!opt.has_value()) {
        throw Error("node not found in graph");
    }
    return *opt;
}

template<GraphNodeId NodeId, typename EdgeProperty>
void DependencyGraph<NodeId, EdgeProperty>::computeDistancesFrom(const NodeId & target) const
{
    // Check if already computed for this target (idempotent)
    if (cachedDistances.has_value() && distanceTarget.has_value() && *distanceTarget == target) {
        return;
    }

    auto targetVertex = getVertexOrThrow(target);
    size_t n = boost::num_vertices(graph);

    std::vector<size_t> distances(n, std::numeric_limits<size_t>::max());
    distances[targetVertex] = 0;

    // Use reverse_graph to follow incoming edges
    auto reversedGraph = boost::make_reverse_graph(graph);

    // Create uniform weight map (all edges have weight 1)
    auto weightMap =
        boost::make_constant_property<typename boost::graph_traits<decltype(reversedGraph)>::edge_descriptor>(1);

    // Run Dijkstra on reversed graph with uniform weights
    boost::dijkstra_shortest_paths(
        reversedGraph,
        targetVertex,
        boost::weight_map(weightMap).distance_map(
            boost::make_iterator_property_map(distances.begin(), boost::get(boost::vertex_index, reversedGraph))));

    cachedDistances = std::move(distances);
    distanceTarget = target;
}

template<GraphNodeId NodeId, typename EdgeProperty>
template<typename NodeVisitor, typename EdgeVisitor, typename StopPredicate>
void DependencyGraph<NodeId, EdgeProperty>::dfsFromTarget(
    const NodeId & start,
    const NodeId & target,
    NodeVisitor && visitNode,
    EdgeVisitor && visitEdge,
    StopPredicate && shouldStop) const
{
    computeDistancesFrom(target);

    std::function<bool(const NodeId &, size_t)> dfs = [&](const NodeId & node, size_t depth) -> bool {
        // Visit node - if returns false, skip this subtree
        if (!visitNode(node, depth)) {
            return false;
        }

        // Check if we should stop the entire traversal
        if (shouldStop(node)) {
            return true; // Signal to stop
        }

        // Get and sort successors by distance
        auto successors = getSuccessors(node);
        auto sortedSuccessors = successors | std::views::transform([&](const auto & ref) -> std::pair<size_t, NodeId> {
                                    auto v = getVertexOrThrow(ref);
                                    return {(*cachedDistances)[v], ref};
                                })
                                | std::views::filter([](const auto & p) {
                                      // Filter unreachable nodes
                                      return p.first != std::numeric_limits<size_t>::max();
                                  })
                                | std::ranges::to<std::vector>();

        std::ranges::sort(sortedSuccessors);

        // Visit each edge and recurse
        for (size_t i = 0; i < sortedSuccessors.size(); ++i) {
            const auto & [dist, successor] = sortedSuccessors[i];
            bool isLast = (i == sortedSuccessors.size() - 1);

            visitEdge(node, successor, isLast, depth);

            if (dfs(successor, depth + 1)) {
                return true; // Propagate stop signal
            }
        }

        return false; // Continue traversal
    };

    dfs(start, 0);
}

template<GraphNodeId NodeId, typename EdgeProperty>
std::vector<NodeId> DependencyGraph<NodeId, EdgeProperty>::getSuccessors(const NodeId & node) const
{
    auto v = getVertexOrThrow(node);
    auto [adjBegin, adjEnd] = boost::adjacent_vertices(v, graph);

    return std::ranges::subrange(adjBegin, adjEnd) | std::views::transform([&](auto v) { return getNodeId(v); })
           | std::ranges::to<std::vector>();
}

template<GraphNodeId NodeId, typename EdgeProperty>
std::optional<EdgeProperty>
DependencyGraph<NodeId, EdgeProperty>::getEdgeProperty(const NodeId & from, const NodeId & to) const
    requires(!std::same_as<EdgeProperty, boost::no_property>)
{
    auto vFrom = getVertexOrThrow(from);
    auto vTo = getVertexOrThrow(to);

    auto [edge, found] = boost::edge(vFrom, vTo, graph);
    if (!found) {
        return std::nullopt;
    }

    return graph[edge];
}

template<GraphNodeId NodeId, typename EdgeProperty>
std::vector<std::vector<NodeId>> DependencyGraph<NodeId, EdgeProperty>::findCycles() const
{
    using vertex_descriptor = typename boost::graph_traits<Graph>::vertex_descriptor;
    using edge_descriptor = typename boost::graph_traits<Graph>::edge_descriptor;

    std::vector<std::vector<vertex_descriptor>> cycleDescriptors;
    std::vector<vertex_descriptor> dfsPath;

    // Custom DFS visitor to detect back edges and extract cycles
    class CycleFinder : public boost::default_dfs_visitor
    {
    public:
        std::vector<std::vector<vertex_descriptor>> & cycles;
        std::vector<vertex_descriptor> & dfsPath;

        CycleFinder(std::vector<std::vector<vertex_descriptor>> & cycles, std::vector<vertex_descriptor> & dfsPath)
            : cycles(cycles)
            , dfsPath(dfsPath)
        {
        }

        void discover_vertex(vertex_descriptor v, const Graph & g)
        {
            dfsPath.push_back(v);
        }

        void finish_vertex(vertex_descriptor v, const Graph & g)
        {
            if (!dfsPath.empty() && dfsPath.back() == v) {
                dfsPath.pop_back();
            }
        }

        void back_edge(edge_descriptor e, const Graph & g)
        {
            auto target = boost::target(e, g);
            auto cycleStart = std::ranges::find(dfsPath, target);
            std::vector<vertex_descriptor> cycle(cycleStart, dfsPath.end());
            cycle.push_back(target);
            cycles.push_back(std::move(cycle));
        }
    };

    CycleFinder visitor(cycleDescriptors, dfsPath);
    boost::depth_first_search(graph, boost::visitor(visitor));

    // Convert vertex_descriptors to NodeIds using ranges
    return cycleDescriptors | std::views::transform([&](const auto & cycleVerts) {
               return cycleVerts | std::views::transform([&](auto v) { return getNodeId(v); })
                      | std::ranges::to<std::vector<NodeId>>();
           })
           | std::ranges::to<std::vector>();
}

template<GraphNodeId NodeId, typename EdgeProperty>
std::vector<NodeId> DependencyGraph<NodeId, EdgeProperty>::getAllNodes() const
{
    return nodeToVertex | std::views::keys | std::ranges::to<std::vector>();
}

} // namespace nix
