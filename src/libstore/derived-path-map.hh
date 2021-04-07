#pragma once

#include "types.hh"
#include "derived-path.hh"

namespace nix {

template<typename V>
struct DerivedPathMap {
	struct Node;
    using ChildMap = std::map<std::string, Node>;
    struct Node {
        V value;
        ChildMap childMap;
    };

    using Raw = std::map<StorePath, Node>;
    Raw map;

    Node & ensureSlot(const SingleDerivedPath & k);
};

}
