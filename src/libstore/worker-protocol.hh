#pragma once

namespace nix {


#define WORKER_MAGIC_1 0x6e697863
#define WORKER_MAGIC_2 0x6478696f

#define PROTOCOL_VERSION 0x117
#define GET_PROTOCOL_MAJOR(x) ((x) & 0xff00)
#define GET_PROTOCOL_MINOR(x) ((x) & 0x00ff)


typedef enum {
    wopIsValidPath = 1,
    wopHasSubstitutes = 3,
    wopQueryPathHash = 4, // obsolete
    wopQueryReferences = 5, // obsolete
    wopQueryReferrers = 6,
    wopAddToStore = 7,
    wopAddTextToStore = 8,
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

std::string read(const Store & store, Source & from, Phantom<std::string> _);
void write(const Store & store, Sink & out, const std::string & str);

StorePath read(const Store & store, Source & from, Phantom<StorePath> _);
void write(const Store & store, Sink & out, const StorePath & storePath);

ContentAddress read(const Store & store, Source & from, Phantom<ContentAddress> _);
void write(const Store & store, Sink & out, const ContentAddress & ca);

template<typename T>
std::set<T> read(const Store & store, Source & from, Phantom<std::set<T>> _);
template<typename T>
void write(const Store & store, Sink & out, const std::set<T> & resSet);

template<typename K, typename V>
std::map<K, V> read(const Store & store, Source & from, Phantom<std::map<K, V>> _);
template<typename K, typename V>
void write(const Store & store, Sink & out, const std::map<K, V> & resMap);

template<typename T>
std::optional<T> read(const Store & store, Source & from, Phantom<std::optional<T>> _);
template<typename T>
void write(const Store & store, Sink & out, const std::optional<T> & optVal);

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
        resMap.insert_or_assign(
            read(store, from, Phantom<K> {}),
            read(store, from, Phantom<V> {}));
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

template<typename T>
std::optional<T> read(const Store & store, Source & from, Phantom<std::optional<T>> _)
{
    auto tag = readNum<uint8_t>(from);
    switch (tag) {
    case 0:
        return std::nullopt;
    case 1:
        return read(store, from, Phantom<T> {});
    default:
        throw Error("got an invalid tag bit for std::optional: %#04x", (size_t)tag);
    }
}

template<typename T>
void write(const Store & store, Sink & out, const std::optional<T> & optVal)
{
    out << (uint64_t) (optVal ? 1 : 0);
    if (optVal)
        nix::worker_proto::write(store, out, *optVal);
}


}


}
