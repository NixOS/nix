#pragma once
/**
 * @file
 *
 * Template implementations (as opposed to mere declarations).
 *
 * This file is an exmample of the "impl.hh" pattern. See the
 * contributing guide.
 */

#include "common-protocol.hh"
#include "length-prefixed-protocol-helper.hh"

namespace nix {

/* protocol-agnostic templates */

#define COMMON_USE_LENGTH_PREFIX_SERIALISER(TEMPLATE, T) \
    TEMPLATE T CommonProto::Serialise< T >::read(const StoreDirConfig & store, CommonProto::ReadConn conn) \
    { \
        return LengthPrefixedProtoHelper<CommonProto, T >::read(store, conn); \
    } \
    TEMPLATE void CommonProto::Serialise< T >::write(const StoreDirConfig & store, CommonProto::WriteConn conn, const T & t) \
    { \
        LengthPrefixedProtoHelper<CommonProto, T >::write(store, conn, t); \
    }

COMMON_USE_LENGTH_PREFIX_SERIALISER(template<typename T>, std::vector<T>)
COMMON_USE_LENGTH_PREFIX_SERIALISER(template<typename T>, std::set<T>)
COMMON_USE_LENGTH_PREFIX_SERIALISER(template<typename... Ts>, std::tuple<Ts...>)

#define COMMA_ ,
COMMON_USE_LENGTH_PREFIX_SERIALISER(
    template<typename K COMMA_ typename V>,
    std::map<K COMMA_ V>)
#undef COMMA_


/* protocol-specific templates */

}
