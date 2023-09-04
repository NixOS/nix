#include "derived-path-map.hh"

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

}

// instantiations

#include "create-derivation-and-realise-goal.hh"
namespace nix {

template struct DerivedPathMap<std::weak_ptr<CreateDerivationAndRealiseGoal>>;

}
