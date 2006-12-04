#ifndef __WORKER_PROTOCOL_H
#define __WORKER_PROTOCOL_H


#define WORKER_MAGIC_1 0x6e697864
#define WORKER_MAGIC_2 0x6478696e


typedef enum {
    wopQuit,
    wopIsValidPath,
    wopQuerySubstitutes,
    wopHasSubstitutes,
    wopQueryPathHash,
    wopQueryReferences,
    wopQueryReferrers,
    wopAddToStore,
    wopAddTextToStore,
    wopBuildDerivations,
    wopEnsurePath,
    wopAddTempRoot,
    wopAddIndirectRoot,
    wopSyncWithGC,
} WorkerOp;


#define STDERR_NEXT  0x6f6c6d67
#define STDERR_LAST  0x616c7473
#define STDERR_ERROR 0x63787470


/* The default location of the daemon socket, relative to
   nixStateDir. */
#define DEFAULT_SOCKET_PATH "/daemon.socket"


#endif /* !__WORKER_PROTOCOL_H */
