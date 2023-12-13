#pragma once
///@file

#include "serialise.hh"

namespace nix {

struct StoreDirConfig;
struct Source;

// items being serialized
class StorePath;
struct ContentAddress;
struct DrvOutput;
struct Realisation;


/**
 * Shared serializers between the worker protocol, serve protocol, and a
 * few others.
 *
 * This `struct` is basically just a `namespace`; We use a type rather
 * than a namespace just so we can use it as a template argument.
 */
struct CommonProto
{
    /**
     * A unidirectional read connection, to be used by the read half of the
     * canonical serializers below.
     */
    struct ReadConn {
        Source & from;
    };

    /**
     * A unidirectional write connection, to be used by the write half of the
     * canonical serializers below.
     */
    struct WriteConn {
        Sink & to;
    };

    template<typename T>
    struct Serialise;

    /**
     * Wrapper function around `CommonProto::Serialise<T>::write` that allows us to
     * infer the type instead of having to write it down explicitly.
     */
    template<typename T>
    static void write(const StoreDirConfig & store, WriteConn conn, const T & t)
    {
        CommonProto::Serialise<T>::write(store, conn, t);
    }
};

#define DECLARE_COMMON_SERIALISER(T) \
    struct CommonProto::Serialise< T > \
    { \
        static T read(const StoreDirConfig & store, CommonProto::ReadConn conn); \
        static void write(const StoreDirConfig & store, CommonProto::WriteConn conn, const T & str); \
    }

template<>
DECLARE_COMMON_SERIALISER(std::string);
template<>
DECLARE_COMMON_SERIALISER(StorePath);
template<>
DECLARE_COMMON_SERIALISER(ContentAddress);
template<>
DECLARE_COMMON_SERIALISER(DrvOutput);
template<>
DECLARE_COMMON_SERIALISER(Realisation);

template<typename T>
DECLARE_COMMON_SERIALISER(std::vector<T>);
template<typename T>
DECLARE_COMMON_SERIALISER(std::set<T>);
template<typename... Ts>
DECLARE_COMMON_SERIALISER(std::tuple<Ts...>);

#define COMMA_ ,
template<typename K, typename V>
DECLARE_COMMON_SERIALISER(std::map<K COMMA_ V>);
#undef COMMA_

/**
 * These use the empty string for the null case, relying on the fact
 * that the underlying types never serialize to the empty string.
 *
 * We do this instead of a generic std::optional<T> instance because
 * ordinal tags (0 or 1, here) are a bit of a compatability hazard. For
 * the same reason, we don't have a std::variant<T..> instances (ordinal
 * tags 0...n).
 *
 * We could the generic instances and then these as specializations for
 * compatability, but that's proven a bit finnicky, and also makes the
 * worker protocol harder to implement in other languages where such
 * specializations may not be allowed.
 */
template<>
DECLARE_COMMON_SERIALISER(std::optional<StorePath>);
template<>
DECLARE_COMMON_SERIALISER(std::optional<ContentAddress>);

}
