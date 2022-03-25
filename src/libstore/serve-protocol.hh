#pragma once

#include "common-protocol.hh"

namespace nix {

#define SERVE_MAGIC_1 0x390c9deb
#define SERVE_MAGIC_2 0x5452eecb

#define SERVE_PROTOCOL_VERSION (2 << 8 | 7)
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


class Store;
struct Source;

// items being serialized
struct DerivedPath;
struct DrvOutput;
struct Realisation;


namespace serve_proto {
/* FIXME maybe move more stuff inside here */

struct ReadConn : common_proto::ReadConn {
};

struct WriteConn : common_proto::WriteConn {
};

#define MAKE_SERVE_PROTO(TEMPLATE, T) \
    TEMPLATE T read(const Store & store, ReadConn conn, Phantom< T > _); \
    TEMPLATE void write(const Store & store, WriteConn conn, const T & str)

MAKE_SERVE_PROTO(, std::string);
MAKE_SERVE_PROTO(, StorePath);
MAKE_SERVE_PROTO(, ContentAddress);
MAKE_SERVE_PROTO(, DerivedPath);
MAKE_SERVE_PROTO(, Realisation);
MAKE_SERVE_PROTO(, DrvOutput);

MAKE_SERVE_PROTO(template<typename T>, std::vector<T>);
MAKE_SERVE_PROTO(template<typename T>, std::set<T>);

#define X_ template<typename K, typename V>
#define Y_ std::map<K, V>
MAKE_SERVE_PROTO(X_, Y_);
#undef X_
#undef Y_

/* See note in common-protocol.hh
 */
MAKE_SERVE_PROTO(, std::optional<StorePath>);
MAKE_SERVE_PROTO(, std::optional<ContentAddress>);

/* These are a non-standard form for historical reasons. */

}

}
