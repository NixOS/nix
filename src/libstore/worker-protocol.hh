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
struct Proxy {};

template<typename T>
std::set<T> read(const Store & store, Source & from, Proxy<std::set<T>> _)
{
    std::set<T> resSet;
    auto size = readNum<size_t>(from);
    while (size--) {
        resSet.insert(read(store, from, Proxy<T> {}));
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
std::map<K, V> read(const Store & store, Source & from, Proxy<std::map<K, V>> _)
{
    std::map<K, V> resMap;
    auto size = readNum<size_t>(from);
    while (size--) {
        resMap.insert_or_assign(
            read(store, from, Proxy<K> {}),
            read(store, from, Proxy<V> {}));
    }
    return resMap;
}

template<typename K, typename V>
void write(const Store & store, Sink & out, const std::map<K, V> & resMap)
{
    out << resMap.size();
    for (auto & [key, value] : resMap) {
        write(store, out, key);
        write(store, out, value);
    }
}

template<typename T>
std::optional<T> read(const Store & store, Source & from, Proxy<std::optional<T>> _)
{
    auto tag = readNum<uint8_t>(from);
    switch (tag) {
    case 0:
        return std::nullopt;
    case 1:
        return read(store, from, Proxy<T> {});
    default:
        throw Error("got an invalid tag bit for std::optional: %#04x", (size_t)tag);
    }
}

template<typename T>
void write(const Store & store, Sink & out, const std::optional<T> & optVal)
{
    out << (uint64_t) (optVal ? 1 : 0);
    if (optVal)
        write(store, out, *optVal);
}

std::string read(const Store & store, Source & from, Proxy<std::string> _);

void write(const Store & store, Sink & out, const std::string & str);

StorePath read(const Store & store, Source & from, Proxy<StorePath> _);

void write(const Store & store, Sink & out, const StorePath & storePath);

StorePathDescriptor read(const Store & store, Source & from, Proxy<StorePathDescriptor> _);

void write(const Store & store, Sink & out, const StorePathDescriptor & ca);

}
