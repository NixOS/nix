#pragma once
///@file

#include "nix/store/common-protocol.hh"

namespace nix {

#define SERVE_MAGIC_1 0x390c9deb
#define SERVE_MAGIC_2 0x5452eecb

#define SERVE_PROTOCOL_VERSION (2 << 8 | 7)
#define GET_PROTOCOL_MAJOR(x) ((x) & 0xff00)
#define GET_PROTOCOL_MINOR(x) ((x) & 0x00ff)

struct StoreDirConfig;
struct Source;

// items being serialised
struct BuildResult;
struct UnkeyedValidPathInfo;

/**
 * The "serve protocol", used by ssh:// stores.
 *
 * This `struct` is basically just a `namespace`; We use a type rather
 * than a namespace just so we can use it as a template argument.
 */
struct ServeProto
{
    /**
     * Enumeration of all the request types for the protocol.
     */
    enum struct Command : uint64_t;

    /**
     * Version type for the protocol.
     *
     * @todo Convert to struct with separate major vs minor fields.
     */
    using Version = unsigned int;

    /**
     * A unidirectional read connection, to be used by the read half of the
     * canonical serializers below.
     */
    struct ReadConn
    {
        Source & from;
        Version version;
    };

    /**
     * A unidirectional write connection, to be used by the write half of the
     * canonical serializers below.
     */
    struct WriteConn
    {
        Sink & to;
        Version version;
    };

    /**
     * Stripped down serialization logic suitable for sharing with Hydra.
     *
     * @todo remove once Hydra uses Store abstraction consistently.
     */
    struct BasicClientConnection;
    struct BasicServerConnection;

    /**
     * Data type for canonical pairs of serialisers for the serve protocol.
     *
     * See https://en.cppreference.com/w/cpp/language/adl for the broader
     * concept of what is going on here.
     */
    template<typename T>
    struct Serialise;
    // This is the definition of `Serialise` we *want* to put here, but
    // do not do so.
    //
    // See `worker-protocol.hh` for a longer explanation.
#if 0
    {
        static T read(const StoreDirConfig & store, ReadConn conn);
        static void write(const StoreDirConfig & store, WriteConn conn, const T & t);
    };
#endif

    /**
     * Wrapper function around `ServeProto::Serialise<T>::write` that allows us to
     * infer the type instead of having to write it down explicitly.
     */
    template<typename T>
    static void write(const StoreDirConfig & store, WriteConn conn, const T & t)
    {
        ServeProto::Serialise<T>::write(store, conn, t);
    }

    /**
     * Options for building shared between
     * `ServeProto::Command::BuildPaths` and
     * `ServeProto::Command::BuildDerivation`.
     */
    struct BuildOptions;
};

enum struct ServeProto::Command : uint64_t {
    QueryValidPaths = 1,
    QueryPathInfos = 2,
    DumpStorePath = 3,
    /**
     * @note This is no longer used by Nix (as a client), but it is used
     * by Hydra. We should therefore not remove it until Hydra no longer
     * uses it either.
     */
    ImportPaths = 4,
    // ExportPaths = 5,
    BuildPaths = 6,
    QueryClosure = 7,
    BuildDerivation = 8,
    AddToStoreNar = 9,
};

struct ServeProto::BuildOptions
{
    /**
     * Default value in this and every other field is so tests pass when
     * testing older deserialisers which do not set all the fields.
     */
    time_t maxSilentTime = -1;
    time_t buildTimeout = -1;
    size_t maxLogSize = -1;
    size_t nrRepeats = -1;
    bool enforceDeterminism = -1;
    bool keepFailed = -1;

    bool operator==(const ServeProto::BuildOptions &) const = default;
};

/**
 * Convenience for sending operation codes.
 *
 * @todo Switch to using `ServeProto::Serialize` instead probably. But
 * this was not done at this time so there would be less churn.
 */
inline Sink & operator<<(Sink & sink, ServeProto::Command op)
{
    return sink << (uint64_t) op;
}

/**
 * Convenience for debugging.
 *
 * @todo Perhaps render known opcodes more nicely.
 */
inline std::ostream & operator<<(std::ostream & s, ServeProto::Command op)
{
    return s << (uint64_t) op;
}

/**
 * Declare a canonical serialiser pair for the worker protocol.
 *
 * We specialise the struct merely to indicate that we are implementing
 * the function for the given type.
 *
 * Some sort of `template<...>` must be used with the caller for this to
 * be legal specialization syntax. See below for what that looks like in
 * practice.
 */
#define DECLARE_SERVE_SERIALISER(T)                                                               \
    struct ServeProto::Serialise<T>                                                               \
    {                                                                                             \
        static T read(const StoreDirConfig & store, ServeProto::ReadConn conn);                   \
        static void write(const StoreDirConfig & store, ServeProto::WriteConn conn, const T & t); \
    };

template<>
DECLARE_SERVE_SERIALISER(BuildResult);
template<>
DECLARE_SERVE_SERIALISER(UnkeyedValidPathInfo);
template<>
DECLARE_SERVE_SERIALISER(ServeProto::BuildOptions);

template<typename T>
DECLARE_SERVE_SERIALISER(std::vector<T>);
#define COMMA_ ,
template<typename T, typename Compare>
DECLARE_SERVE_SERIALISER(std::set<T COMMA_ Compare>);
template<typename... Ts>
DECLARE_SERVE_SERIALISER(std::tuple<Ts...>);

template<typename K, typename V>
DECLARE_SERVE_SERIALISER(std::map<K COMMA_ V>);
#undef COMMA_

} // namespace nix
