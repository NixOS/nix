#pragma once

namespace nix {


#define WORKER_MAGIC_1 0x6e697863
#define WORKER_MAGIC_2 0x6478696f

#define PROTOCOL_VERSION 0x115
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
    wopQueryDerivationOutputs = 22,
    wopQueryAllValidPaths = 23,
    wopQueryFailedPaths = 24,
    wopClearFailedPaths = 25,
    wopQueryPathInfo = 26,
    wopImportPaths = 27, // obsolete
    wopQueryDerivationOutputNames = 28,
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
} WorkerOp;


#define STDERR_NEXT  0x6f6c6d67
#define STDERR_READ  0x64617461 // data needed from source
#define STDERR_WRITE 0x64617416 // data for sink
#define STDERR_LAST  0x616c7473
#define STDERR_ERROR 0x63787470
#define STDERR_START_ACTIVITY 0x53545254
#define STDERR_STOP_ACTIVITY  0x53544f50
#define STDERR_RESULT         0x52534c54


Path readStorePath(Store & store, Source & from);
template<class T> T readStorePaths(Store & store, Source & from);


}
