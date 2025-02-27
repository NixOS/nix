#pragma once
///@file

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
 * value. `* const Something` or `std::optional<Something>` would be
 * good choices for "optional" types.
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

        bool operator == (const ChildNode &) const noexcept;

        // TODO libc++ 16 (used by darwin) missing `std::map::operator <=>`, can't do yet.
        // decltype(std::declval<V>() <=> std::declval<V>())
        // operator <=> (const ChildNode &) const noexcept;
    };

    /**
     * The map type for the root node.
     */
    using Map = std::map<StorePath, ChildNode>;

    /**
     * The map of root node.
     */
    Map map;

    bool operator == (const DerivedPathMap &) const = default;

    // TODO libc++ 16 (used by darwin) missing `std::map::operator <=>`, can't do yet.
    // auto operator <=> (const DerivedPathMap &) const noexcept;

    /**
     * Find the node for `k`, creating it if needed.
     *
     * The node is referred to as a "slot" on the assumption that `V` is
     * some sort of optional type, so the given key can be set or unset
     * by changing this node.
     */
    ChildNode & ensureSlot(const SingleDerivedPath & k);

    /**
     * Like `ensureSlot` but does not create the slot if it doesn't exist.
     *
     * Read the entire description of `ensureSlot` to understand an
     * important caveat here that "have slot" does *not* imply "key is
     * set in map". To ensure a key is set one would need to get the
     * child node (with `findSlot` or `ensureSlot`) *and* check the
     * `ChildNode::value`.
     */
    ChildNode * findSlot(const SingleDerivedPath & k);
};

template<>
bool DerivedPathMap<std::set<std::string>>::ChildNode::operator == (
    const DerivedPathMap<std::set<std::string>>::ChildNode &) const noexcept;

// TODO libc++ 16 (used by darwin) missing `std::map::operator <=>`, can't do yet.
#if 0
template<>
std::strong_ordering DerivedPathMap<std::set<std::string>>::ChildNode::operator <=> (
    const DerivedPathMap<std::set<std::string>>::ChildNode &) const noexcept;

template<>
inline auto DerivedPathMap<std::set<std::string>>::operator <=> (const DerivedPathMap<std::set<std::string>> &) const noexcept = default;
#endif

extern template struct DerivedPathMap<std::set<std::string>>::ChildNode;
extern template struct DerivedPathMap<std::set<std::string>>;

}
