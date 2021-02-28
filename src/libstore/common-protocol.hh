#pragma once

#include "serialise.hh"
#include "phantom.hh"

namespace nix {

class Store;
struct Source;

// items being serialized
struct DerivedPath;
struct DrvOutput;
struct Realisation;
struct BuildResult;


namespace common_proto {
/* FIXME maybe move more stuff inside here */

struct ReadConn {
   Source & from;
};

struct WriteConn {
   Sink & to;
};

#define MAKE_PROTO(TEMPLATE, T) \
    TEMPLATE T read(const Store & store, ReadConn conn, Phantom< T > _); \
    TEMPLATE void write(const Store & store, WriteConn conn, const T & str)

MAKE_PROTO(, std::string);
MAKE_PROTO(, StorePath);
MAKE_PROTO(, ContentAddress);
MAKE_PROTO(, DerivedPath);
MAKE_PROTO(, Realisation);
MAKE_PROTO(, DrvOutput);

MAKE_PROTO(template<typename T>, std::vector<T>);
MAKE_PROTO(template<typename T>, std::set<T>);

#define X_ template<typename K, typename V>
#define Y_ std::map<K, V>
MAKE_PROTO(X_, Y_);
#undef X_
#undef Y_

/* These use the empty string for the null case, relying on the fact
   that the underlying types never serialize to the empty string.

   We do this instead of a generic std::optional<T> instance because
   ordinal tags (0 or 1, here) are a bit of a compatability hazard. For
   the same reason, we don't have a std::variant<T..> instances (ordinal
   tags 0...n).

   We could the generic instances and then these as specializations for
   compatability, but that's proven a bit finnicky, and also makes the
   worker protocol harder to implement in other languages where such
   specializations may not be allowed.
 */
MAKE_PROTO(, std::optional<StorePath>);
MAKE_PROTO(, std::optional<ContentAddress>);

/* N suffix indicates this is the nth version of them in common. Wrapped downstream
   by protocol-specific functions which handle the previous versions too. */

BuildResult read0(const Store & store, ReadConn conn, Phantom<BuildResult> _);
void write0(const Store & store, WriteConn conn, const BuildResult & res);

}

}
