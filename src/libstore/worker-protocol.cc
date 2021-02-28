#include "serialise.hh"
#include "util.hh"
#include "path-with-outputs.hh"
#include "store-api.hh"
#include "build-result.hh"
#include "worker-protocol.hh"
#include "worker-protocol-impl.hh"
#include "archive.hh"
#include "path-info.hh"

#include <nlohmann/json.hpp>

namespace nix {
namespace worker_proto {

/* protocol-specific definitions */

KeyedBuildResult read(const Store & store, ReadConn conn, Phantom<KeyedBuildResult> _)
{
    auto path = worker_proto::read(store, conn, Phantom<DerivedPath> {});
    auto br = worker_proto::read(store, conn, Phantom<BuildResult> {});
    return KeyedBuildResult {
        std::move(br),
        .path = std::move(path),
    };
}

void write(const Store & store, WriteConn conn, const KeyedBuildResult & res)
{
    worker_proto::write(store, conn, res.path);
    worker_proto::write(store, conn, static_cast<const BuildResult &>(res));
}

BuildResult read(const Store & store, ReadConn conn, Phantom<BuildResult> _)
{
    if (GET_PROTOCOL_MINOR(conn.version) < 29) {
        BuildResult res;
        res.status = (BuildResult::Status) readInt(conn.from);
        conn.from >> res.errorMsg;
        if (GET_PROTOCOL_MINOR(conn.version) >= 28) {
            auto builtOutputs = read(store, conn, Phantom<DrvOutputs> {});
            res.builtOutputs = builtOutputs;
        }
        return res;
    } else
        return read0(store, conn, Phantom<BuildResult>{});
}

void write(const Store & store, WriteConn conn, const BuildResult & res)
{
    if (GET_PROTOCOL_MINOR(conn.version) < 29) {
        conn.to << res.status << res.errorMsg;
        if (GET_PROTOCOL_MINOR(conn.version) >= 28) {
            write(store, conn, res.builtOutputs);
        }
    } else
        write0(store, conn, res);
}


ValidPathInfo readValidPathInfo(const Store & store, ReadConn conn)
{
    auto path = read(store, conn, Phantom<StorePath>{});
    return readValidPathInfo(store, conn, std::move(path));
}

ValidPathInfo readValidPathInfo(const Store & store, ReadConn conn, StorePath && path)
{
    auto deriver = readString(conn.from);
    auto narHash = Hash::parseAny(readString(conn.from), htSHA256);
    ValidPathInfo info(path, narHash);
    if (deriver != "") info.deriver = store.parseStorePath(deriver);
    info.references = read(store, conn, Phantom<StorePathSet> {});
    conn.from >> info.registrationTime >> info.narSize;
    if (GET_PROTOCOL_MINOR(conn.version) >= 16) {
        conn.from >> info.ultimate;
        info.sigs = readStrings<StringSet>(conn.from);
        info.ca = parseContentAddressOpt(readString(conn.from));
    }
    return info;
}

void write(
    const Store & store,
    WriteConn conn,
    const ValidPathInfo & pathInfo,
    bool includePath)
{
    if (includePath)
        conn.to << store.printStorePath(pathInfo.path);
    conn.to
        << (pathInfo.deriver ? store.printStorePath(*pathInfo.deriver) : "")
        << pathInfo.narHash.to_string(Base16, false);
    write(store, conn, pathInfo.references);
    conn.to << pathInfo.registrationTime << pathInfo.narSize;
    if (GET_PROTOCOL_MINOR(conn.version) >= 16) {
        conn.to
            << pathInfo.ultimate
            << pathInfo.sigs
            << renderContentAddress(pathInfo.ca);
    }
}

}
}
