#include "derived-path-map.hh"

namespace nix {

template<typename V>
typename DerivedPathMap<V>::Node & DerivedPathMap<V>::ensureSlot(const SingleDerivedPath & k)
{
    std::function<Node &(const SingleDerivedPath & )> initIter;
    initIter = [&](const auto & k) -> auto & {
        return std::visit(overloaded {
            [&](const SingleDerivedPath::Opaque & bo) -> auto & {
                // will not overrwrite if already there
                return map[bo.path];
            },
            [&](const SingleDerivedPath::Built & bfd) -> auto & {
                auto & n = initIter(*bfd.drvPath);
                return n.childMap[bfd.outputs];
            },
        }, k.raw());
    };
    return initIter(k);
}

}

// instantiations

#include "outer-derivation-goal.hh"
namespace nix {

template struct DerivedPathMap<std::set<std::string>>;
template struct DerivedPathMap<std::weak_ptr<OuterDerivationGoal>>;

}
