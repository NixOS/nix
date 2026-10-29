#include "nix/store/derivation/full-inputs.hh"
#include "nix/store/derived-path.hh"
#include "nix/util/util.hh"

#include <algorithm>
#include <ranges>

namespace nix::derivation {

std::set<SingleDerivedPath> FullInputs::toSet() const
{
    std::set<SingleDerivedPath> result;

    // Add source paths
    for (const auto & src : srcs) {
        result.insert(SingleDerivedPath::Opaque{src});
    }

    // Add derivation outputs - need to recursively handle the DerivedPathMap
    auto addNode = [&](this auto && self,
                       ref<const SingleDerivedPath> drvPath,
                       const DerivedPathMap<std::set<OutputName, std::less<>>>::ChildNode & node) -> void {
        for (const auto & outputName : node.value) {
            result.insert(SingleDerivedPath::Built{drvPath, outputName});
        }
        for (const auto & [outputName, childNode] : node.childMap) {
            self(make_ref<SingleDerivedPath>(SingleDerivedPath::Built{drvPath, outputName}), childNode);
        }
    };

    for (const auto & [drvPath, node] : drvs.map) {
        addNode(make_ref<SingleDerivedPath>(SingleDerivedPath::Opaque{drvPath}), node);
    }

    return result;
}

FullInputs FullInputs::fromSet(const std::set<SingleDerivedPath> & inputs)
{
    FullInputs result;

    using ChildNode = DerivedPathMap<std::set<OutputName, std::less<>>>::ChildNode;

    /* Find (creating as needed) the node for `path`, recursing to the
       root first so that the innermost output level ends up nearest the
       root (matching `toSet`). */
    auto nodeFor = [&](this auto && self, ref<const SingleDerivedPath> path) -> ChildNode & {
        return std::visit(
            overloaded{
                [&](const SingleDerivedPath::Opaque & op) -> ChildNode & { return result.drvs.map[op.path]; },
                [&](const SingleDerivedPath::Built & parentBuilt) -> ChildNode & {
                    return self(parentBuilt.drvPath).childMap[parentBuilt.output];
                }},
            path->raw());
    };

    for (const auto & input : inputs) {
        std::visit(
            overloaded{
                [&](const SingleDerivedPath::Opaque & op) { result.srcs.insert(op.path); },
                [&](const SingleDerivedPath::Built & built) { nodeFor(built.drvPath).value.insert(built.output); }},
            input.raw());
    }

    return result;
}

bool hasDynamicDrvDep(const FullInputs & inputs)
{
    return std::ranges::any_of(inputs.drvs.map, [](auto & kv) { return !kv.second.childMap.empty(); });
}

} // namespace nix::derivation
