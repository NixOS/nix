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


namespace serve_proto {

using common_proto::read;
using common_proto::write;

/* FIXME maybe move more stuff inside here */

struct ReadConn : common_proto::ReadConn {
};

struct WriteConn : common_proto::WriteConn {
};

MAKE_PROTO(template<typename T>, std::vector<T>);
MAKE_PROTO(template<typename T>, std::set<T>);

#define X_ template<typename K, typename V>
#define Y_ std::map<K, V>
MAKE_PROTO(X_, Y_);
#undef X_
#undef Y_

/* These are a non-standard form for historical reasons. */

}

}
