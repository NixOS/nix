#pragma once

#include "store-api.hh"
#include "serialise.hh"

namespace nix {


#define WORKER_MAGIC_1 0x6e697863
#define WORKER_MAGIC_2 0x6478696f

#define PROTOCOL_VERSION (1 << 8 | 34)
#define GET_PROTOCOL_MAJOR(x) ((x) & 0xff00)
#define GET_PROTOCOL_MINOR(x) ((x) & 0x00ff)


typedef enum {
    wopIsValidPath = 1,
    wopHasSubstitutes = 3,
    wopQueryPathHash = 4, // obsolete
    wopQueryReferences = 5, // obsolete
    wopQueryReferrers = 6,
    wopAddToStore = 7,
    wopAddTextToStore = 8, // obsolete since 1.25, Nix 3.0. Use wopAddToStore
    wopBuildPaths = 9,
    wopEnsurePath = 10,
    wopAddTempRoot = 11,
    wopAddIndirectRoot = 12,
    wopSyncWithGC = 13,
    wopFindRoots = 14,
    wopExportPath = 16, // obsolete
    wopQueryDeriver = 18, // obsolete
    wopSetOptions = 19,
    wopCollectGarbage = 20,
    wopQuerySubstitutablePathInfo = 21,
    wopQueryDerivationOutputs = 22, // obsolete
    wopQueryAllValidPaths = 23,
    wopQueryFailedPaths = 24,
    wopClearFailedPaths = 25,
    wopQueryPathInfo = 26,
    wopImportPaths = 27, // obsolete
    wopQueryDerivationOutputNames = 28, // obsolete
    wopQueryPathFromHashPart = 29,
    wopQuerySubstitutablePathInfos = 30,
    wopQueryValidPaths = 31,
    wopQuerySubstitutablePaths = 32,
    wopQueryValidDerivers = 33,
    wopOptimiseStore = 34,
    wopVerifyStore = 35,
    wopBuildDerivation = 36,
    wopAddSignatures = 37,
    wopNarFromPath = 38,
    wopAddToStoreNar = 39,
    wopQueryMissing = 40,
    wopQueryDerivationOutputMap = 41,
    wopRegisterDrvOutput = 42,
    wopQueryRealisation = 43,
    wopAddMultipleToStore = 44,
    wopAddBuildLog = 45,
    wopBuildPathsWithResults = 46,
} WorkerOp;


#define STDERR_NEXT  0x6f6c6d67
#define STDERR_READ  0x64617461 // data needed from source
#define STDERR_WRITE 0x64617416 // data for sink
#define STDERR_LAST  0x616c7473
#define STDERR_ERROR 0x63787470
#define STDERR_START_ACTIVITY 0x53545254
#define STDERR_STOP_ACTIVITY  0x53544f50
#define STDERR_RESULT         0x52534c54


class Store;
struct Source;

/* To guide overloading */
template<typename T>
struct Phantom {};


namespace worker_proto {
/* FIXME maybe move more stuff inside here */

#define MAKE_WORKER_PROTO(TEMPLATE, T) \
    TEMPLATE T read(const Store & store, Source & from, Phantom< T > _); \
    TEMPLATE void write(const Store & store, Sink & out, const T & str)

MAKE_WORKER_PROTO(, std::string);
MAKE_WORKER_PROTO(, StorePath);
MAKE_WORKER_PROTO(, ContentAddress);
MAKE_WORKER_PROTO(, StorePathDescriptor);
MAKE_WORKER_PROTO(, DerivedPath);
MAKE_WORKER_PROTO(, Realisation);
MAKE_WORKER_PROTO(, DrvOutput);
MAKE_WORKER_PROTO(, BuildResult);

MAKE_WORKER_PROTO(template<typename T>, std::vector<T>);
MAKE_WORKER_PROTO(template<typename T>, std::set<T>);

#define X_ template<typename K, typename V>
#define Y_ std::map<K, V>
MAKE_WORKER_PROTO(X_, Y_);
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
MAKE_WORKER_PROTO(, std::optional<StorePath>);
MAKE_WORKER_PROTO(, std::optional<ContentAddress>);

template<typename T>
std::vector<T> read(const Store & store, Source & from, Phantom<std::vector<T>> _)
{
    std::vector<T> resSet;
    auto size = readNum<size_t>(from);
    while (size--) {
        resSet.push_back(read(store, from, Phantom<T> {}));
    }
    return resSet;
}

template<typename T>
void write(const Store & store, Sink & out, const std::vector<T> & resSet)
{
    out << resSet.size();
    for (auto & key : resSet) {
        write(store, out, key);
    }
}

template<typename T>
std::set<T> read(const Store & store, Source & from, Phantom<std::set<T>> _)
{
    std::set<T> resSet;
    auto size = readNum<size_t>(from);
    while (size--) {
        resSet.insert(read(store, from, Phantom<T> {}));
    }
    return resSet;
}

template<typename T>
void write(const Store & store, Sink & out, const std::set<T> & resSet)
{
    out << resSet.size();
    for (auto & key : resSet) {
        write(store, out, key);
    }
}

template<typename K, typename V>
std::map<K, V> read(const Store & store, Source & from, Phantom<std::map<K, V>> _)
{
    std::map<K, V> resMap;
    auto size = readNum<size_t>(from);
    while (size--) {
        auto k = read(store, from, Phantom<K> {});
        auto v = read(store, from, Phantom<V> {});
        resMap.insert_or_assign(std::move(k), std::move(v));
    }
    return resMap;
}

template<typename K, typename V>
void write(const Store & store, Sink & out, const std::map<K, V> & resMap)
{
    out << resMap.size();
    for (auto & i : resMap) {
        write(store, out, i.first);
        write(store, out, i.second);
    }
}

}

}
