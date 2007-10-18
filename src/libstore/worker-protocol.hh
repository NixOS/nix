#ifndef __WORKER_PROTOCOL_H
#define __WORKER_PROTOCOL_H


namespace nix {


#define WORKER_MAGIC_1 0x6e697863
#define WORKER_MAGIC_2 0x6478696f

#define PROTOCOL_VERSION 0x101
#define GET_PROTOCOL_MAJOR(x) ((x) & 0xff00)


typedef enum {
    wopQuit = 0,							//0
    wopIsValidPath,							//1
	wopHasSubstitutes = 3,					//3
    wopIsValidStatePath,
    wopIsValidComponentOrStatePath,
    wopQueryPathHash,
    wopQueryStatePathDrv,
    wopQueryStoreReferences,
    wopQueryStateReferences,
    wopQueryStoreReferrers,					//10
    wopQueryStateReferrers,					
    wopAddToStore,							
    wopAddTextToStore,						//13
    wopBuildDerivations,					//14
    wopEnsurePath,							//15
    wopAddTempRoot,
    wopAddIndirectRoot,
    wopSyncWithGC,
    wopFindRoots,
    wopCollectGarbage,						//20
    wopExportPath,							
    wopImportPath,
    wopQueryDeriver,
    wopQueryDerivers,
    wopSetStatePathsInterval,
	wopGetStatePathsInterval,
	wopIsStateComponent,
	wopStorePathRequisites,
	wopSetStateRevisions,
	wopQueryStateRevisions,					//30
	wopQueryAvailableStateRevisions,		
	wopCommitStatePath,
	wopScanAndUpdateAllReferences,
	wopGetSharedWith,
	wopToNonSharedPathSet,
	wopRevertToRevision,
	wopShareState,
	wopUnShareState,						
    wopSetOptions,							//39
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
Path readStatePath(Source & from);
Path readStoreOrStatePath(Source & from);

PathSet readStorePaths(Source & from);
PathSet readStatePaths(Source & from);

    
}


#endif /* !__WORKER_PROTOCOL_H */
