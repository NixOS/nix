#include "serialise.hh"
#include "path-with-outputs.hh"
#include "store-api.hh"
#include "build-result.hh"
#include "serve-protocol.hh"
#include "serve-protocol-impl.hh"
#include "archive.hh"
#include "path-info.hh"

#include <nlohmann/json.hpp>

namespace nix {

/* protocol-specific definitions */

BuildResult ServeProto::Serialise<BuildResult>::read(const StoreDirConfig & store, ServeProto::ReadConn conn)
{
    BuildResult status;
    status.status = (BuildResult::Status) readInt(conn.from);
    conn.from >> status.errorMsg;

    if (GET_PROTOCOL_MINOR(conn.version) >= 3)
        conn.from
            >> status.timesBuilt
            >> status.isNonDeterministic
            >> status.startTime
            >> status.stopTime;
    if (GET_PROTOCOL_MINOR(conn.version) >= 6) {
        auto builtOutputs = ServeProto::Serialise<DrvOutputs>::read(store, conn);
        for (auto && [output, realisation] : builtOutputs)
            status.builtOutputs.insert_or_assign(
                std::move(output.outputName),
                std::move(realisation));
    }
    return status;
}

void ServeProto::Serialise<BuildResult>::write(const StoreDirConfig & store, ServeProto::WriteConn conn, const BuildResult & status)
{
    conn.to
        << status.status
        << status.errorMsg;

    if (GET_PROTOCOL_MINOR(conn.version) >= 3)
        conn.to
            << status.timesBuilt
            << status.isNonDeterministic
            << status.startTime
            << status.stopTime;
    if (GET_PROTOCOL_MINOR(conn.version) >= 6) {
        DrvOutputs builtOutputs;
        for (auto & [output, realisation] : status.builtOutputs)
            builtOutputs.insert_or_assign(realisation.id, realisation);
        ServeProto::write(store, conn, builtOutputs);
    }
}


UnkeyedValidPathInfo ServeProto::Serialise<UnkeyedValidPathInfo>::read(const StoreDirConfig & store, ReadConn conn)
{
    /* Hash should be set below unless very old `nix-store --serve`.
       Caller should assert that it did set it. */
    UnkeyedValidPathInfo info { Hash::dummy };

    auto deriver = readString(conn.from);
    if (deriver != "")
        info.deriver = store.parseStorePath(deriver);
    info.references = ServeProto::Serialise<StorePathSet>::read(store, conn);

    readLongLong(conn.from); // download size, unused
    info.narSize = readLongLong(conn.from);

    if (GET_PROTOCOL_MINOR(conn.version) >= 4) {
        auto s = readString(conn.from);
        if (!s.empty())
            info.narHash = Hash::parseAnyPrefixed(s);
        info.ca = ContentAddress::parseOpt(readString(conn.from));
        info.sigs = readStrings<StringSet>(conn.from);
    }

    return info;
}

void ServeProto::Serialise<UnkeyedValidPathInfo>::write(const StoreDirConfig & store, WriteConn conn, const UnkeyedValidPathInfo & info)
{
    conn.to
        << (info.deriver ? store.printStorePath(*info.deriver) : "");

    ServeProto::write(store, conn, info.references);
    // !!! Maybe we want compression?
    conn.to
        << info.narSize // downloadSize, lie a little
        << info.narSize;
    if (GET_PROTOCOL_MINOR(conn.version) >= 4)
        conn.to
            << info.narHash.to_string(HashFormat::Nix32, true)
            << renderContentAddress(info.ca)
            << info.sigs;
}


ServeProto::BuildOptions ServeProto::Serialise<ServeProto::BuildOptions>::read(const StoreDirConfig & store, ReadConn conn)
{
    BuildOptions options;
    options.maxSilentTime = readInt(conn.from);
    options.buildTimeout = readInt(conn.from);
    if (GET_PROTOCOL_MINOR(conn.version) >= 2)
        options.maxLogSize = readNum<unsigned long>(conn.from);
    if (GET_PROTOCOL_MINOR(conn.version) >= 3) {
        options.nrRepeats = readInt(conn.from);
        options.enforceDeterminism = readInt(conn.from);
    }
    if (GET_PROTOCOL_MINOR(conn.version) >= 7) {
        options.keepFailed = (bool) readInt(conn.from);
    }
    return options;
}

void ServeProto::Serialise<ServeProto::BuildOptions>::write(const StoreDirConfig & store, WriteConn conn, const ServeProto::BuildOptions & options)
{
    conn.to
        << options.maxSilentTime
        << options.buildTimeout;
    if (GET_PROTOCOL_MINOR(conn.version) >= 2)
        conn.to
            << options.maxLogSize;
    if (GET_PROTOCOL_MINOR(conn.version) >= 3)
        conn.to
            << options.nrRepeats
            << options.enforceDeterminism;

    if (GET_PROTOCOL_MINOR(conn.version) >= 7) {
        conn.to << ((int) options.keepFailed);
    }
}

}
