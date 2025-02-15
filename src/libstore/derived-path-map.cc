#include "nix/store/derived-path-map.hh"
#include "nix/util/fun.hh"
#include "nix/util/util.hh"

namespace nix {

template<typename V>
typename DerivedPathMap<V>::ChildNode & DerivedPathMap<V>::ensureSlot(const SingleDerivedPath & k)
{
    fun<ChildNode &(const SingleDerivedPath &)> initIter = [&](const auto & k) -> auto & {
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
    fun<ChildNode *(const SingleDerivedPath &)> initIter = [&](const auto & k) {
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

/* A node is still live if it holds a value or has children. The value is a
   container for goal maps but a bare `weak_ptr` for the single-goal trampoline
   maps, so dispatch on which "emptiness" notion applies. */
template<typename ChildNode>
static bool childNodeNonEmpty(const ChildNode & node)
{
    bool valueNonEmpty = [&] {
        if constexpr (requires { node.value.empty(); })
            return !node.value.empty();
        else
            return !node.value.expired();
    }();
    return valueNonEmpty || !node.childMap.empty();
}

template<typename V>
void DerivedPathMap<V>::removeSlot(const SingleDerivedPath & k, fun<bool(ChildNode &)> callback)
{
    auto removeIter = [&map =
                           map](this auto & self, const SingleDerivedPath & k, fun<bool(ChildNode &)> onNode) -> void {
        std::visit(
            overloaded{
                [&](const SingleDerivedPath::Opaque & bo) {
                    if (auto it = map.find(bo.path); it != map.end() && !onNode(it->second))
                        map.erase(it);
                },
                [&](const SingleDerivedPath::Built & bfd) {
                    self(*bfd.drvPath, [&](ChildNode & parent) -> bool {
                        auto it = parent.childMap.find(bfd.output);
                        if (it == parent.childMap.end())
                            return childNodeNonEmpty(parent);
                        if (!onNode(it->second))
                            parent.childMap.erase(it);
                        return childNodeNonEmpty(parent);
                    });
                },
            },
            k.raw());
    };

    removeIter(k, [&](ChildNode & node) -> bool { return callback(node) || !node.childMap.empty(); });
}

} // namespace nix

// instantiations

#include "nix/store/build/build-trace-trampoline-goal.hh"
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
template struct DerivedPathMap<std::map<OutputsSpec, std::weak_ptr<DerivationTrampolineGoal>>>;

}; // namespace nix
