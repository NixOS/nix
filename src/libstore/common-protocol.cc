#include "serialise.hh"
#include "util.hh"
#include "path-with-outputs.hh"
#include "store-api.hh"
#include "build-result.hh"
#include "common-protocol.hh"
#include "common-protocol-impl.hh"
#include "archive.hh"
#include "derivations.hh"

#include <nlohmann/json.hpp>

namespace nix {
namespace common_proto {

/* protocol-agnostic definitions */

std::string read(const Store & store, ReadConn conn, Phantom<std::string> _)
{
    return readString(conn.from);
}

void write(const Store & store, WriteConn conn, const std::string & str)
{
    conn.to << str;
}


StorePath read(const Store & store, ReadConn conn, Phantom<StorePath> _)
{
    return store.parseStorePath(readString(conn.from));
}

void write(const Store & store, WriteConn conn, const StorePath & storePath)
{
    conn.to << store.printStorePath(storePath);
}


ContentAddress read(const Store & store, ReadConn conn, Phantom<ContentAddress> _)
{
    return parseContentAddress(readString(conn.from));
}

void write(const Store & store, WriteConn conn, const ContentAddress & ca)
{
    conn.to << renderContentAddress(ca);
}

DerivedPath read(const Store & store, ReadConn conn, Phantom<DerivedPath> _)
{
    auto s = readString(conn.from);
    return DerivedPath::parse(store, s);
}

void write(const Store & store, WriteConn conn, const DerivedPath & req)
{
    conn.to << req.to_string(store);
}


Realisation read(const Store & store, ReadConn conn, Phantom<Realisation> _)
{
    std::string rawInput = readString(conn.from);
    return Realisation::fromJSON(
        nlohmann::json::parse(rawInput),
        "remote-protocol"
    );
}

void write(const Store & store, WriteConn conn, const Realisation & realisation)
{
    conn.to << realisation.toJSON().dump();
}


DrvOutput read(const Store & store, ReadConn conn, Phantom<DrvOutput> _)
{
    return DrvOutput::parse(readString(conn.from));
}

void write(const Store & store, WriteConn conn, const DrvOutput & drvOutput)
{
    conn.to << drvOutput.to_string();
}


std::optional<StorePath> read(const Store & store, ReadConn conn, Phantom<std::optional<StorePath>> _)
{
    auto s = readString(conn.from);
    return s == "" ? std::optional<StorePath> {} : store.parseStorePath(s);
}

void write(const Store & store, WriteConn conn, const std::optional<StorePath> & storePathOpt)
{
    conn.to << (storePathOpt ? store.printStorePath(*storePathOpt) : "");
}


std::optional<ContentAddress> read(const Store & store, ReadConn conn, Phantom<std::optional<ContentAddress>> _)
{
    return parseContentAddressOpt(readString(conn.from));
}

void write(const Store & store, WriteConn conn, const std::optional<ContentAddress> & caOpt)
{
    conn.to << (caOpt ? renderContentAddress(*caOpt) : "");
}

// Helpers for downstream

BuildResult read0(const Store & store, ReadConn conn, Phantom<BuildResult> _)
{
    BuildResult res;
    res.status = (BuildResult::Status) readInt(conn.from);
    conn.from
        >> res.errorMsg
        >> res.timesBuilt
        >> res.isNonDeterministic
        >> res.startTime
        >> res.stopTime;
    res.builtOutputs = read(store, conn, Phantom<DrvOutputs> {});
    return res;
}

void write0(const Store & store, WriteConn conn, const BuildResult & res)
{
    conn.to
        << res.status
        << res.errorMsg
        << res.timesBuilt
        << res.isNonDeterministic
        << res.startTime
        << res.stopTime;
    write(store, conn, res.builtOutputs);
}

}
}
