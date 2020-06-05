#pragma once

namespace nix {

#define SERVE_MAGIC_1 0x390c9deb
#define SERVE_MAGIC_2 0x5452eecb

#define SERVE_PROTOCOL_VERSION 0x205
#define GET_PROTOCOL_MAJOR(x) ((x) & 0xff00)
#define GET_PROTOCOL_MINOR(x) ((x) & 0x00ff)

typedef enum {
    cmdQueryValidPaths = 1,
    cmdQueryPathInfos = 2,
    cmdDumpStorePath = 3,
    cmdImportPaths = 4,
    cmdExportPaths = 5,
    cmdBuildPaths = 6,
    cmdQueryClosure = 7,
    cmdBuildDerivation = 8,
    cmdAddToStoreNar = 9,
} ServeCommand;

}
