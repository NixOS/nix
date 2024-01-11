#include "derived-path-map.hh"
#include "util.hh"

namespace nix {

template<typename V>
typename DerivedPathMap<V>::ChildNode & DerivedPathMap<V>::ensureSlot(const SingleDerivedPath & k)
{
    std::function<ChildNode &(const SingleDerivedPath & )> initIter;
    initIter = [&](const auto & k) -> auto & {
        return std::visit(overloaded {
            [&](const SingleDerivedPath::Opaque & bo) -> auto & {
                // will not overwrite if already there
                return map[bo.path];
            },
            [&](const SingleDerivedPath::Built & bfd) -> auto & {
                auto & n = initIter(*bfd.drvPath);
                return n.childMap[bfd.output];
            },
        }, k.raw());
    };
    return initIter(k);
}

template<typename V>
typename DerivedPathMap<V>::ChildNode * DerivedPathMap<V>::findSlot(const SingleDerivedPath & k)
{
    std::function<ChildNode *(const SingleDerivedPath & )> initIter;
    initIter = [&](const auto & k) {
        return std::visit(overloaded {
            [&](const SingleDerivedPath::Opaque & bo) {
                auto it = map.find(bo.path);
                return it != map.end()
                    ? &it->second
                    : nullptr;
            },
            [&](const SingleDerivedPath::Built & bfd) {
                auto * n = initIter(*bfd.drvPath);
                if (!n) return (ChildNode *)nullptr;

                auto it = n->childMap.find(bfd.output);
                return it != n->childMap.end()
                    ? &it->second
                    : nullptr;
            },
        }, k.raw());
    };
    return initIter(k);
}

}

// instantiations

namespace nix {

GENERATE_CMP_EXT(
    template<>,
    DerivedPathMap<std::set<std::string>>::ChildNode,
    me->value,
    me->childMap);

GENERATE_CMP_EXT(
    template<>,
    DerivedPathMap<std::set<std::string>>,
    me->map);

template struct DerivedPathMap<std::set<std::string>>;

};
