#pragma once
#include <sys/socket.h>

namespace nix {


#define WORKER_MAGIC_1 0x6e697863
#define WORKER_MAGIC_2 0x6478696f

#define PROTOCOL_VERSION 0x10e
#define RECURSIVE_PROTOCOL_VERSION 0x101
#define GET_PROTOCOL_MAJOR(x) ((x) & 0xff00)
#define GET_PROTOCOL_MINOR(x) ((x) & 0x00ff)


typedef enum {
    wopQuit = 0,
    wopIsValidPath = 1,
    wopHasSubstitutes = 3,
    wopQueryPathHash = 4,
    wopQueryReferences = 5,
    wopQueryReferrers = 6,
    wopAddToStore = 7,
    wopAddTextToStore = 8,
    wopBuildPaths = 9,
    wopEnsurePath = 10,
    wopAddTempRoot = 11,
    wopAddIndirectRoot = 12,
    wopSyncWithGC = 13,
    wopFindRoots = 14,
    wopExportPath = 16,
    wopQueryDeriver = 18,
    wopSetOptions = 19,
    wopCollectGarbage = 20,
    wopQuerySubstitutablePathInfo = 21,
    wopQueryDerivationOutputs = 22,
    wopQueryAllValidPaths = 23,
    wopQueryFailedPaths = 24,
    wopClearFailedPaths = 25,
    wopQueryPathInfo = 26,
    wopImportPaths = 27,
    wopQueryDerivationOutputNames = 28,
    wopQueryPathFromHashPart = 29,
    wopQuerySubstitutablePathInfos = 30,
    wopQueryValidPaths = 31,
    wopQuerySubstitutablePaths = 32,
    wopQueryValidDerivers = 33,
} WorkerOp;


#define STDERR_NEXT  0x6f6c6d67
#define STDERR_READ  0x64617461 // data needed from source
#define STDERR_WRITE 0x64617416 // data for sink
#define STDERR_LAST  0x616c7473
#define STDERR_ERROR 0x63787470


Path readStorePath(Source & from);
template<class T> T readStorePaths(Source & from);


}

/* Borrowed from http://swtch.com/usr/local/plan9/src/lib9/sendfd.c, should
   really be part of POSIX.
   Copyright (C) 2003, Lucent Technologies Inc. and others. All Rights Reserved. */

#ifndef CMSG_ALIGN
#   ifdef __sun__
#       define CMSG_ALIGN _CMSG_DATA_ALIGN
#   else
#       define CMSG_ALIGN(len) (((len)+sizeof(long)-1) & ~(sizeof(long)-1))
#   endif
#endif

#ifndef CMSG_SPACE
#   define CMSG_SPACE(len) (CMSG_ALIGN(sizeof(struct cmsghdr))+CMSG_ALIGN(len))
#endif

#ifndef CMSG_LEN
#   define CMSG_LEN(len) (CMSG_ALIGN(sizeof(struct cmsghdr))+(len))
#endif
