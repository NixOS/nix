#pragma once

#include "meta-protocol-templates.hh"

namespace nix {
namespace common_proto {

/* protocol-agnostic templates */

WRAP_META_PROTO(template<typename T>, std::vector<T>)
WRAP_META_PROTO(template<typename T>, std::set<T>)

#define X_ template<typename K, typename V>
#define Y_ std::map<K, V>
WRAP_META_PROTO(X_, Y_)
#undef X_
#undef Y_

/* protocol-specific templates */

}
}
