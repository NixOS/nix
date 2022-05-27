#pragma once

#include "error.hh"

namespace nix {

template<typename T>
std::vector<T> topoSort(std::set<T> items,
        std::function<std::set<T>(const T &)> getChildren,
        std::function<Error(const T &, const T &)> makeCycleError)
{
    std::vector<T> sorted;
    std::set<T> visited, parents;

    std::function<void(const T & path, const T * parent)> dfsVisit;

    dfsVisit = [&](const T & path, const T * parent) {
        if (parents.count(path)) throw makeCycleError(path, *parent);

        if (!visited.insert(path).second) return;
        parents.insert(path);

        std::set<T> references = getChildren(path);

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



template<typename T>
std::vector<T> topoSortCycles(std::set<T> items,
        std::function<std::set<T>(const T &)> getChildren,
        std::function<void(const T &, const std::vector<T> *)> handleCycle)
{
    std::vector<T> sorted;
    std::set<T> visited, parents;

    std::function<void(const T & path, const T * parent)> dfsVisit;

    dfsVisit = [&](const T & path, const T * parent) {
        debug(format("topoSortCycles: path = %1%") % path);
        if (parent != nullptr) {
            debug(format("topoSortCycles: args: *parent = %1%") % *parent);
        }
        else {
            debug(format("topoSortCycles: args: parent = nullptr") % *parent);
        }
        debug(format("topoSortCycles: state: sorted = %1%") % *parent);

        if (parents.count(path)) {
            debug(format("topoSortCycles: found cycle"));
            for (auto & i : parents) {
                debug(format("topoSortCycles: parents[] = %1%") % i);
            }
            for (auto & i : sorted) {
                debug(format("topoSortCycles: sorted[] = %1%") % i);
            }
            debug(format("topoSortCycles: calling handleCycle"));
            // NOTE sorted can be larger than necessary.
            // ex:
            // sorted: a b c
            // path: b
            // -> cycle b c b
            // -> a is not in cycle
            // -> end of sorted is in cycle
            std::vector<T> * sortedPtr = &sorted;
            handleCycle(path, const_cast<const std::vector<T> *>(sortedPtr));
        }

        if (!visited.insert(path).second) {
            debug(format("topoSortCycles: !visited.insert(path).second == true -> return"));
            return;
        }
        debug(format("topoSortCycles: !visited.insert(path).second == false -> continue"));

        parents.insert(path);
        sorted.push_back(path);

        debug(format("topoSortCycles: calling getChildren"));
        std::set<T> references = getChildren(path);

        for (auto & i : references) {
            debug(format("topoSortCycles: references[] = %1%") % i);
        }

        // recursion
        for (auto & i : references)
            /* Don't traverse into items that don't exist in our starting set. */
            if (i != path && items.count(i)) {
                debug(format("topoSortCycles: dfsVisit? yes"));
                dfsVisit(i, &path);
            }
            else {
                debug(format("topoSortCycles: dfsVisit? no"));
            }

        debug(format("topoSortCycles: done path: %1%") % path);
        //sorted.push_back(path); // moved before recursion, so we have sorted in handleCycle
        parents.erase(path);
        for (auto & i : sorted) {
            debug(format("topoSortCycles: done: sorted[] = %1%") % i);
        }
    };

    for (auto & i : items)
        dfsVisit(i, nullptr);

    std::reverse(sorted.begin(), sorted.end());

    return sorted;
}

}
