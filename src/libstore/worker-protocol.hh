#pragma once

namespace nix {


#define WORKER_MAGIC_1 0x6e697863
#define WORKER_MAGIC_2 0x6478696f

#define PROTOCOL_VERSION 0x119
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

template<typename T>
struct WorkerProto {
    static T read(const Store & store, Source & from);
    static void write(const Store & store, Sink & out, const T & t);
};

#define MAKE_WORKER_PROTO(T) \
    template<> \
    struct WorkerProto< T > { \
        static T read(const Store & store, Source & from); \
        static void write(const Store & store, Sink & out, const T & t); \
    }

MAKE_WORKER_PROTO(std::string);
MAKE_WORKER_PROTO(StorePath);
MAKE_WORKER_PROTO(StorePathDescriptor);

template<typename T>
struct WorkerProto<std::set<T>> {

    static std::set<T> read(const Store & store, Source & from)
    {
        std::set<T> resSet;
        auto size = readNum<size_t>(from);
        while (size--) {
            resSet.insert(WorkerProto<T>::read(store, from));
        }
        return resSet;
    }

    static void write(const Store & store, Sink & out, const std::set<T> & resSet)
    {
        out << resSet.size();
        for (auto & key : resSet) {
            WorkerProto<T>::write(store, out, key);
        }
    }

};

template<typename K, typename V>
struct WorkerProto<std::map<K, V>> {

    static std::map<K, V> read(const Store & store, Source & from)
    {
        std::map<K, V> resMap;
        auto size = readNum<size_t>(from);
        while (size--) {
            auto k = WorkerProto<K>::read(store, from);
            auto v = WorkerProto<V>::read(store, from);
            resMap.insert_or_assign(std::move(k), std::move(v));
        }
        return resMap;
    }

    static void write(const Store & store, Sink & out, const std::map<K, V> & resMap)
    {
        out << resMap.size();
        for (auto & i : resMap) {
            WorkerProto<K>::write(store, out, i.first);
            WorkerProto<V>::write(store, out, i.second);
        }
    }

};

template<typename T>
struct WorkerProto<std::optional<T>> {

    static std::optional<T> read(const Store & store, Source & from)
    {
        auto tag = readNum<uint8_t>(from);
        switch (tag) {
        case 0:
            return std::nullopt;
        case 1:
            return WorkerProto<T>::read(store, from);
        default:
            throw Error("got an invalid tag bit for std::optional: %#04x", (size_t)tag);
        }
    }

    static void write(const Store & store, Sink & out, const std::optional<T> & optVal)
    {
        out << (uint64_t) (optVal ? 1 : 0);
        if (optVal)
            WorkerProto<T>::write(store, out, *optVal);
    }

};

/* Specialization which uses and empty string for the empty case, taking
   advantage of the fact these types always serialize to non-empty strings.
   This is done primarily for backwards compatability, so that T <=
   std::optional<T>, where <= is the compatability partial order, T is one of
   the types below.
 */
MAKE_WORKER_PROTO(std::optional<StorePath>);
MAKE_WORKER_PROTO(std::optional<ContentAddress>);

}
