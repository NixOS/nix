#include "nix/store/derivation/full-inputs.hh"
#include "nix/store/derived-path.hh"
#include "nix/util/util.hh"

namespace nix {

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

    auto findRoot = [](this auto && self, ref<const SingleDerivedPath> p) -> StorePath {
        return std::visit(
            overloaded{
                [&](const SingleDerivedPath::Opaque & op) { return op.path; },
                [&](const SingleDerivedPath::Built & b) { return self(b.drvPath); }},
            p->raw());
    };

    for (const auto & input : inputs) {
        std::visit(
            overloaded{
                [&](const SingleDerivedPath::Opaque & op) { result.srcs.insert(op.path); },
                [&](const SingleDerivedPath::Built & built) {
                    // Find the root derivation path
                    auto rootPath = findRoot(built.drvPath);
                    auto & rootNode = result.drvs.map[rootPath];

                    // Build the nested structure by walking from root to this output
                    auto insertBuilt =
                        [&](this auto && self,
                            ref<const SingleDerivedPath> path,
                            const std::string & output,
                            DerivedPathMap<std::set<OutputName, std::less<>>>::ChildNode & node) -> void {
                        std::visit(
                            overloaded{
                                [&](const SingleDerivedPath::Opaque &) { node.value.insert(output); },
                                [&](const SingleDerivedPath::Built & parentBuilt) {
                                    auto & childNode = node.childMap[parentBuilt.output];
                                    self(parentBuilt.drvPath, output, childNode);
                                }},
                            path->raw());
                    };

                    insertBuilt(built.drvPath, built.output, rootNode);
                }},
            input.raw());
    }

    return result;
}

} // namespace nix
