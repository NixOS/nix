#pragma once

#include "types.hh"
#include "derived-path.hh"

namespace nix {

/**
 * A simple Trie, of sorts. Conceptually a map of `SingleDerivedPath` to
 * values.
 *
 * Concretely, an n-ary tree, as described below. A
 * `SingleDerivedPath::Opaque` maps to the value of an immediate child
 * of the root node. A `SingleDerivedPath::Built` maps to a deeper child
 * node: the `SingleDerivedPath::Built::drvPath` is first mapped to a a
 * child node (inductively), and then the
 * `SingleDerivedPath::Built::output` is used to look up that child's
 * child via its map. In this manner, every `SingleDerivedPath` is
 * mapped to a child node.
 *
 * @param V A type to instantiate for each output. It should probably
 * should be an "optional" type so not every interior node has to have a
 * value. For example, the scheduler uses
 * `DerivedPathMap<std::weak_ptr<CreateDerivationAndRealiseGoal>>` to
 * remember which goals correspond to which outputs. `* const Something`
 * or `std::optional<Something>` would also be good choices for
 * "optional" types.
 */
template<typename V>
struct DerivedPathMap {
    /**
     * A child node (non-root node).
     */
    struct ChildNode {
        /**
         * Value of this child node.
         *
         * @see DerivedPathMap for what `V` should be.
         */
        V value;

        /**
         * The map type for the root node.
         */
        using Map = std::map<OutputName, ChildNode>;

        /**
         * The map of the root node.
         */
        Map childMap;
    };

    /**
     * The map type for the root node.
     */
    using Map = std::map<StorePath, ChildNode>;

    /**
     * The map of root node.
     */
    Map map;

    /**
     * Find the node for `k`, creating it if needed.
     *
     * The node is referred to as a "slot" on the assumption that `V` is
     * some sort of optional type, so the given key can be set or unset
     * by changing this node.
     */
    ChildNode & ensureSlot(const SingleDerivedPath & k);
};

}
