#include "nix/store/derived-path-map.hh"
#include "nix/util/util.hh"

namespace nix {

template<typename V>
typename DerivedPathMap<V>::ChildNode & DerivedPathMap<V>::ensureSlot(const SingleDerivedPath & k)
{
    std::function<ChildNode &(const SingleDerivedPath &)> initIter;
    initIter = [&](const auto & k) -> auto & {
        return std::visit(
            overloaded{
                [&](const SingleDerivedPath::Opaque & bo) -> auto & {
                    // will not overwrite if already there
                    return map[bo.path];
                },
                [&](const SingleDerivedPath::Built & bfd) -> auto & {
                    auto & n = initIter(*bfd.drvPath);
                    return n.childMap[bfd.output];
                },
            },
            k.raw());
    };
    return initIter(k);
}

template<typename V>
typename DerivedPathMap<V>::ChildNode * DerivedPathMap<V>::findSlot(const SingleDerivedPath & k)
{
    std::function<ChildNode *(const SingleDerivedPath &)> initIter;
    initIter = [&](const auto & k) {
        return std::visit(
            overloaded{
                [&](const SingleDerivedPath::Opaque & bo) {
                    auto it = map.find(bo.path);
                    return it != map.end() ? &it->second : nullptr;
                },
                [&](const SingleDerivedPath::Built & bfd) {
                    auto * n = initIter(*bfd.drvPath);
                    if (!n)
                        return (ChildNode *) nullptr;

                    auto it = n->childMap.find(bfd.output);
                    return it != n->childMap.end() ? &it->second : nullptr;
                },
            },
            k.raw());
    };
    return initIter(k);
}

} // namespace nix

// instantiations

#include "nix/store/build/build-trace-trampoline-goal.hh"
#include "nix/store/build/derived-output-goal.hh"
#include "nix/store/build/derivation-trampoline-goal.hh"

namespace nix {

template<>
bool DerivedPathMap<StringSet>::ChildNode::operator==(const DerivedPathMap<StringSet>::ChildNode &) const noexcept =
    default;

// TODO libc++ 16 (used by darwin) missing `std::map::operator <=>`, can't do yet.
#if 0
template<>
std::strong_ordering DerivedPathMap<StringSet>::ChildNode::operator <=> (
    const DerivedPathMap<StringSet>::ChildNode &) const noexcept = default;
#endif

template struct DerivedPathMap<StringSet>::ChildNode;
template struct DerivedPathMap<StringSet>;

template struct DerivedPathMap<std::weak_ptr<BuildTraceTrampolineGoal>>;
template struct DerivedPathMap<std::weak_ptr<DerivedOutputGoal>>;
template struct DerivedPathMap<std::map<OutputsSpec, std::weak_ptr<DerivationTrampolineGoal>>>;

}; // namespace nix
