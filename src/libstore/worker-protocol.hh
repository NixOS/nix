#pragma once
///@file

#include <chrono>

#include "common-protocol.hh"

namespace nix {


#define WORKER_MAGIC_1 0x6e697863
#define WORKER_MAGIC_2 0x6478696f

#define PROTOCOL_VERSION (1 << 8 | 38)
#define GET_PROTOCOL_MAJOR(x) ((x) & 0xff00)
#define GET_PROTOCOL_MINOR(x) ((x) & 0x00ff)


#define STDERR_NEXT  0x6f6c6d67
#define STDERR_READ  0x64617461 // data needed from source
#define STDERR_WRITE 0x64617416 // data for sink
#define STDERR_LAST  0x616c7473
#define STDERR_ERROR 0x63787470
#define STDERR_START_ACTIVITY 0x53545254
#define STDERR_STOP_ACTIVITY  0x53544f50
#define STDERR_RESULT         0x52534c54


struct StoreDirConfig;
struct Source;

// items being serialised
struct DerivedPath;
struct BuildResult;
struct KeyedBuildResult;
struct ValidPathInfo;
struct UnkeyedValidPathInfo;
enum TrustedFlag : bool;


/**
 * The "worker protocol", used by unix:// and ssh-ng:// stores.
 *
 * This `struct` is basically just a `namespace`; We use a type rather
 * than a namespace just so we can use it as a template argument.
 */
struct WorkerProto
{
    /**
     * Enumeration of all the request types for the protocol.
     */
    enum struct Op : uint64_t;

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
    struct ReadConn {
        Source & from;
        Version version;
    };

    /**
     * A unidirectional write connection, to be used by the write half of the
     * canonical serializers below.
     */
    struct WriteConn {
        Sink & to;
        Version version;
    };

    /**
     * Data type for canonical pairs of serialisers for the worker protocol.
     *
     * See https://en.cppreference.com/w/cpp/language/adl for the broader
     * concept of what is going on here.
     */
    template<typename T>
    struct Serialise;
    // This is the definition of `Serialise` we *want* to put here, but
    // do not do so.
    //
    // The problem is that if we do so, C++ will think we have
    // seralisers for *all* types. We don't, of course, but that won't
    // cause an error until link time. That makes for long debug cycles
    // when there is a missing serialiser.
    //
    // By not defining it globally, and instead letting individual
    // serialisers specialise the type, we get back the compile-time
    // errors we would like. When no serialiser exists, C++ sees an
    // abstract "incomplete" type with no definition, and any attempt to
    // use `to` or `from` static methods is a compile-time error because
    // they don't exist on an incomplete type.
    //
    // This makes for a quicker debug cycle, as desired.
#if 0
    {
        static T read(const StoreDirConfig & store, ReadConn conn);
        static void write(const StoreDirConfig & store, WriteConn conn, const T & t);
    };
#endif

    /**
     * Wrapper function around `WorkerProto::Serialise<T>::write` that allows us to
     * infer the type instead of having to write it down explicitly.
     */
    template<typename T>
    static void write(const StoreDirConfig & store, WriteConn conn, const T & t)
    {
        WorkerProto::Serialise<T>::write(store, conn, t);
    }
};

enum struct WorkerProto::Op : uint64_t
{
    IsValidPath = 1,
    HasSubstitutes = 3,
    QueryPathHash = 4, // obsolete
    QueryReferences = 5, // obsolete
    QueryReferrers = 6,
    AddToStore = 7,
    AddTextToStore = 8, // obsolete since 1.25, Nix 3.0. Use WorkerProto::Op::AddToStore
    BuildPaths = 9,
    EnsurePath = 10,
    AddTempRoot = 11,
    AddIndirectRoot = 12,
    SyncWithGC = 13,
    FindRoots = 14,
    ExportPath = 16, // obsolete
    QueryDeriver = 18, // obsolete
    SetOptions = 19,
    CollectGarbage = 20,
    QuerySubstitutablePathInfo = 21,
    QueryDerivationOutputs = 22, // obsolete
    QueryAllValidPaths = 23,
    QueryFailedPaths = 24,
    ClearFailedPaths = 25,
    QueryPathInfo = 26,
    ImportPaths = 27, // obsolete
    QueryDerivationOutputNames = 28, // obsolete
    QueryPathFromHashPart = 29,
    QuerySubstitutablePathInfos = 30,
    QueryValidPaths = 31,
    QuerySubstitutablePaths = 32,
    QueryValidDerivers = 33,
    OptimiseStore = 34,
    VerifyStore = 35,
    BuildDerivation = 36,
    AddSignatures = 37,
    NarFromPath = 38,
    AddToStoreNar = 39,
    QueryMissing = 40,
    QueryDerivationOutputMap = 41,
    RegisterDrvOutput = 42,
    QueryRealisation = 43,
    AddMultipleToStore = 44,
    AddBuildLog = 45,
    BuildPathsWithResults = 46,
    AddPermRoot = 47,
};

/**
 * Convenience for sending operation codes.
 *
 * @todo Switch to using `WorkerProto::Serialise` instead probably. But
 * this was not done at this time so there would be less churn.
 */
inline Sink & operator << (Sink & sink, WorkerProto::Op op)
{
    return sink << static_cast<uint64_t>(op);
}

/**
 * Convenience for debugging.
 *
 * @todo Perhaps render known opcodes more nicely.
 */
inline std::ostream & operator << (std::ostream & s, WorkerProto::Op op)
{
    return s << static_cast<uint64_t>(op);
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
#define DECLARE_WORKER_SERIALISER(T) \
    struct WorkerProto::Serialise< T > \
    { \
        static T read(const StoreDirConfig & store, WorkerProto::ReadConn conn); \
        static void write(const StoreDirConfig & store, WorkerProto::WriteConn conn, const T & t); \
    };

template<>
DECLARE_WORKER_SERIALISER(DerivedPath);
template<>
DECLARE_WORKER_SERIALISER(BuildResult);
template<>
DECLARE_WORKER_SERIALISER(KeyedBuildResult);
template<>
DECLARE_WORKER_SERIALISER(ValidPathInfo);
template<>
DECLARE_WORKER_SERIALISER(UnkeyedValidPathInfo);
template<>
DECLARE_WORKER_SERIALISER(std::optional<TrustedFlag>);
template<>
DECLARE_WORKER_SERIALISER(std::optional<std::chrono::microseconds>);

template<typename T>
DECLARE_WORKER_SERIALISER(std::vector<T>);
template<typename T>
DECLARE_WORKER_SERIALISER(std::set<T>);
template<typename... Ts>
DECLARE_WORKER_SERIALISER(std::tuple<Ts...>);

#define COMMA_ ,
template<typename K, typename V>
DECLARE_WORKER_SERIALISER(std::map<K COMMA_ V>);
#undef COMMA_

}
