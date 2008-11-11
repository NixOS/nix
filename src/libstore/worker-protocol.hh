#ifndef __WORKER_PROTOCOL_H
#define __WORKER_PROTOCOL_H


namespace nix {


#define WORKER_MAGIC_1 0x6e697863
#define WORKER_MAGIC_2 0x6478696f

#define PROTOCOL_VERSION 0x104
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
    wopBuildDerivations = 9,
    wopEnsurePath = 10,
    wopAddTempRoot = 11,
    wopAddIndirectRoot = 12,
    wopSyncWithGC = 13,
    wopFindRoots = 14,
    wopExportPath = 16,
    wopImportPath = 17,
    wopQueryDeriver = 18,
    wopSetOptions = 19,
    wopCollectGarbage = 20,
    wopQuerySubstitutablePathInfo = 21,
} WorkerOp;


#define STDERR_NEXT  0x6f6c6d67
#define STDERR_READ  0x64617461 // data needed from source
#define STDERR_WRITE 0x64617416 // data for sink
#define STDERR_LAST  0x616c7473
#define STDERR_ERROR 0x63787470


/* The default location of the daemon socket, relative to nixStateDir.
   The socket is in a directory to allow you to control access to the
   Nix daemon by setting the mode/ownership of the directory
   appropriately.  (This wouldn't work on the socket itself since it
   must be deleted and recreated on startup.) */
#define DEFAULT_SOCKET_PATH "/daemon-socket/socket"


Path readStorePath(Source & from);
PathSet readStorePaths(Source & from);

    
}


#endif /* !__WORKER_PROTOCOL_H */
