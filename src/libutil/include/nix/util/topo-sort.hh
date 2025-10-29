#pragma once
///@file

#include "nix/util/error.hh"

namespace nix {

template<typename T, typename Compare>
std::vector<T> topoSort(
    std::set<T, Compare> items,
    std::function<std::set<T, Compare>(const T &)> getChildren,
    std::function<Error(const T &, const T &)> makeCycleError)
{
    std::vector<T> sorted;
    decltype(items) visited, parents;

    std::function<void(const T & path, const T * parent)> dfsVisit;

    dfsVisit = [&](const T & path, const T * parent) {
        if (parents.count(path))
            throw makeCycleError(path, *parent);

        if (!visited.insert(path).second)
            return;
        parents.insert(path);

        auto references = getChildren(path);

        for (auto & i : references)
            /* Don't traverse into items that don't exist in our starting set. */
            if (i != path && items.count(i))
                dfsVisit(i, &path);

        sorted.push_back(path);
        parents.erase(path);
    };

    for (auto & i : items)
        dfsVisit(i, nullptr);

    std::reverse(sorted.begin(), sorted.end());

    return sorted;
}

} // namespace nix
