#include "serialise.hh"
#include "util.hh"
#include "path-with-outputs.hh"
#include "store-api.hh"
#include "build-result.hh"
#include "worker-protocol.hh"
#include "worker-protocol-impl.hh"
#include "archive.hh"
#include "derivations.hh"

#include <nlohmann/json.hpp>

namespace nix {

std::string WorkerProto::Serialise<std::string>::read(const Store & store, WorkerProto::ReadConn conn)
{
    return readString(conn.from);
}

void WorkerProto::Serialise<std::string>::write(const Store & store, WorkerProto::WriteConn conn, const std::string & str)
{
    conn.to << str;
}


StorePath WorkerProto::Serialise<StorePath>::read(const Store & store, WorkerProto::ReadConn conn)
{
    return store.parseStorePath(readString(conn.from));
}

void WorkerProto::Serialise<StorePath>::write(const Store & store, WorkerProto::WriteConn conn, const StorePath & storePath)
{
    conn.to << store.printStorePath(storePath);
}


std::optional<TrustedFlag> WorkerProto::Serialise<std::optional<TrustedFlag>>::read(const Store & store, WorkerProto::ReadConn conn)
{
    auto temp = readNum<uint8_t>(conn.from);
    switch (temp) {
        case 0:
            return std::nullopt;
        case 1:
            return { Trusted };
        case 2:
            return { NotTrusted };
        default:
            throw Error("Invalid trusted status from remote");
    }
}

void WorkerProto::Serialise<std::optional<TrustedFlag>>::write(const Store & store, WorkerProto::WriteConn conn, const std::optional<TrustedFlag> & optTrusted)
{
    if (!optTrusted)
        conn.to << (uint8_t)0;
    else {
        switch (*optTrusted) {
        case Trusted:
            conn.to << (uint8_t)1;
            break;
        case NotTrusted:
            conn.to << (uint8_t)2;
            break;
        default:
            assert(false);
        };
    }
}


ContentAddress WorkerProto::Serialise<ContentAddress>::read(const Store & store, WorkerProto::ReadConn conn)
{
    return ContentAddress::parse(readString(conn.from));
}

void WorkerProto::Serialise<ContentAddress>::write(const Store & store, WorkerProto::WriteConn conn, const ContentAddress & ca)
{
    conn.to << renderContentAddress(ca);
}


DerivedPath WorkerProto::Serialise<DerivedPath>::read(const Store & store, WorkerProto::ReadConn conn)
{
    auto s = readString(conn.from);
    return DerivedPath::parseLegacy(store, s);
}

void WorkerProto::Serialise<DerivedPath>::write(const Store & store, WorkerProto::WriteConn conn, const DerivedPath & req)
{
    conn.to << req.to_string_legacy(store);
}


Realisation WorkerProto::Serialise<Realisation>::read(const Store & store, WorkerProto::ReadConn conn)
{
    std::string rawInput = readString(conn.from);
    return Realisation::fromJSON(
        nlohmann::json::parse(rawInput),
        "remote-protocol"
    );
}

void WorkerProto::Serialise<Realisation>::write(const Store & store, WorkerProto::WriteConn conn, const Realisation & realisation)
{
    conn.to << realisation.toJSON().dump();
}


DrvOutput WorkerProto::Serialise<DrvOutput>::read(const Store & store, WorkerProto::ReadConn conn)
{
    return DrvOutput::parse(readString(conn.from));
}

void WorkerProto::Serialise<DrvOutput>::write(const Store & store, WorkerProto::WriteConn conn, const DrvOutput & drvOutput)
{
    conn.to << drvOutput.to_string();
}


KeyedBuildResult WorkerProto::Serialise<KeyedBuildResult>::read(const Store & store, WorkerProto::ReadConn conn)
{
    auto path = WorkerProto::Serialise<DerivedPath>::read(store, conn);
    auto br = WorkerProto::Serialise<BuildResult>::read(store, conn);
    return KeyedBuildResult {
        std::move(br),
        /* .path = */ std::move(path),
    };
}

void WorkerProto::Serialise<KeyedBuildResult>::write(const Store & store, WorkerProto::WriteConn conn, const KeyedBuildResult & res)
{
    WorkerProto::write(store, conn, res.path);
    WorkerProto::write(store, conn, static_cast<const BuildResult &>(res));
}


BuildResult WorkerProto::Serialise<BuildResult>::read(const Store & store, WorkerProto::ReadConn conn)
{
    BuildResult res;
    res.status = (BuildResult::Status) readInt(conn.from);
    conn.from
        >> res.errorMsg
        >> res.timesBuilt
        >> res.isNonDeterministic
        >> res.startTime
        >> res.stopTime;
    auto builtOutputs = WorkerProto::Serialise<DrvOutputs>::read(store, conn);
    for (auto && [output, realisation] : builtOutputs)
        res.builtOutputs.insert_or_assign(
            std::move(output.outputName),
            std::move(realisation));
    return res;
}

void WorkerProto::Serialise<BuildResult>::write(const Store & store, WorkerProto::WriteConn conn, const BuildResult & res)
{
    conn.to
        << res.status
        << res.errorMsg
        << res.timesBuilt
        << res.isNonDeterministic
        << res.startTime
        << res.stopTime;
    DrvOutputs builtOutputs;
    for (auto & [output, realisation] : res.builtOutputs)
        builtOutputs.insert_or_assign(realisation.id, realisation);
    WorkerProto::write(store, conn, builtOutputs);
}


std::optional<StorePath> WorkerProto::Serialise<std::optional<StorePath>>::read(const Store & store, WorkerProto::ReadConn conn)
{
    auto s = readString(conn.from);
    return s == "" ? std::optional<StorePath> {} : store.parseStorePath(s);
}

void WorkerProto::Serialise<std::optional<StorePath>>::write(const Store & store, WorkerProto::WriteConn conn, const std::optional<StorePath> & storePathOpt)
{
    conn.to << (storePathOpt ? store.printStorePath(*storePathOpt) : "");
}


std::optional<ContentAddress> WorkerProto::Serialise<std::optional<ContentAddress>>::read(const Store & store, WorkerProto::ReadConn conn)
{
    return ContentAddress::parseOpt(readString(conn.from));
}

void WorkerProto::Serialise<std::optional<ContentAddress>>::write(const Store & store, WorkerProto::WriteConn conn, const std::optional<ContentAddress> & caOpt)
{
    conn.to << (caOpt ? renderContentAddress(*caOpt) : "");
}

}
