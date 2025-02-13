#pragma once
/**
 * @file
 *
 * Template implementations (as opposed to mere declarations).
 *
 * This file is an example of the "impl.hh" pattern. See the
 * contributing guide.
 */

#include "nix/store/common-protocol.hh"
#include "nix/store/length-prefixed-protocol-helper.hh"

namespace nix {

/* protocol-agnostic templates */

#define COMMON_USE_LENGTH_PREFIX_SERIALISER(TEMPLATE, T)                                                 \
    TEMPLATE T CommonProto::Serialise<T>::read(const StoreDirConfig & store, CommonProto::ReadConn conn) \
    {                                                                                                    \
        return LengthPrefixedProtoHelper<CommonProto, T>::read(store, conn);                             \
    }                                                                                                    \
    TEMPLATE void CommonProto::Serialise<T>::write(                                                      \
        const StoreDirConfig & store, CommonProto::WriteConn conn, const T & t)                          \
    {                                                                                                    \
        LengthPrefixedProtoHelper<CommonProto, T>::write(store, conn, t);                                \
    }

COMMON_USE_LENGTH_PREFIX_SERIALISER(template<typename T>, std::vector<T>)
#define COMMA_ ,
COMMON_USE_LENGTH_PREFIX_SERIALISER(template<typename T COMMA_ typename Compare>, std::set<T COMMA_ Compare>)
COMMON_USE_LENGTH_PREFIX_SERIALISER(template<typename... Ts>, std::tuple<Ts...>)

COMMON_USE_LENGTH_PREFIX_SERIALISER(
    template<typename K COMMA_ typename V COMMA_ typename Compare>, std::map<K COMMA_ V COMMA_ Compare>)
#undef COMMA_

/* protocol-specific templates */

} // namespace nix
