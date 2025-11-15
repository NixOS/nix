#pragma once
///@file

#include "nix/util/error.hh"
#include <variant>

namespace nix {

template<typename T>
struct Cycle
{
    T path;
    T parent;
};

template<typename T>
using TopoSortResult = std::variant<std::vector<T>, Cycle<T>>;

template<typename T, typename Compare>
TopoSortResult<T> topoSort(std::set<T, Compare> items, std::function<std::set<T, Compare>(const T &)> getChildren)
{
    std::vector<T> sorted;
    decltype(items) visited, parents;

    std::function<std::optional<Cycle<T>>(const T & path, const T * parent)> dfsVisit;

    dfsVisit = [&](const T & path, const T * parent) -> std::optional<Cycle<T>> {
        if (parents.count(path)) {
            return Cycle{path, *parent};
        }

        if (!visited.insert(path).second) {
            return std::nullopt;
        }
        parents.insert(path);

        auto references = getChildren(path);

        for (auto & i : references)
            /* Don't traverse into items that don't exist in our starting set. */
            if (i != path && items.count(i)) {
                auto result = dfsVisit(i, &path);
                if (result.has_value()) {
                    return result;
                }
            }

        sorted.push_back(path);
        parents.erase(path);

        return std::nullopt;
    };

    for (auto & i : items) {
        auto cycle = dfsVisit(i, nullptr);
        if (cycle.has_value()) {
            return *cycle;
        }
    }

    std::reverse(sorted.begin(), sorted.end());

    return sorted;
}

} // namespace nix
