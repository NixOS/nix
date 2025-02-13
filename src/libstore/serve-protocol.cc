#include "nix/util/serialise.hh"
#include "nix/store/path-with-outputs.hh"
#include "nix/store/store-api.hh"
#include "nix/store/build-result.hh"
#include "nix/store/serve-protocol.hh"
#include "nix/store/serve-protocol-impl.hh"
#include "nix/util/archive.hh"
#include "nix/store/path-info.hh"

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
    if (GET_PROTOCOL_MINOR(conn.version) >= 8) {
        status.builtOutputs = ServeProto::Serialise<std::map<OutputName, UnkeyedRealisation>>::read(store, conn);
    } else if (GET_PROTOCOL_MINOR(conn.version) >= 6) {
        // We no longer support these types of realisations
        (void) ServeProto::Serialise<StringMap>::read(store, conn);
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
    if (GET_PROTOCOL_MINOR(conn.version) >= 8) {
        ServeProto::write(store, conn, status.builtOutputs);
    } else if (GET_PROTOCOL_MINOR(conn.version) >= 6) {
        // We no longer support these types of realisations
        ServeProto::write(store, conn, StringMap{});
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


UnkeyedRealisation ServeProto::Serialise<UnkeyedRealisation>::read(const StoreDirConfig & store, ReadConn conn)
{
    if (GET_PROTOCOL_MINOR(conn.version) < 39) {
        throw Error("daemon protocol %d.%d is too old (< 1.29) to understand build trace",
            GET_PROTOCOL_MAJOR(conn.version),
            GET_PROTOCOL_MINOR(conn.version));
    }

    auto outPath = ServeProto::Serialise<StorePath>::read(store, conn);
    auto signatures = ServeProto::Serialise<StringSet>::read(store, conn);

    return UnkeyedRealisation {
        .outPath = std::move(outPath),
        .signatures = std::move(signatures),
    };
}

void ServeProto::Serialise<UnkeyedRealisation>::write(const StoreDirConfig & store, WriteConn conn, const UnkeyedRealisation & info)
{
    if (GET_PROTOCOL_MINOR(conn.version) < 39) {
        throw Error("daemon protocol %d.%d is too old (< 1.29) to understand build trace",
            GET_PROTOCOL_MAJOR(conn.version),
            GET_PROTOCOL_MINOR(conn.version));
    }
    ServeProto::write(store, conn, info.outPath);
    ServeProto::write(store, conn, info.signatures);
}


DrvOutput ServeProto::Serialise<DrvOutput>::read(const StoreDirConfig & store, ReadConn conn)
{
    if (GET_PROTOCOL_MINOR(conn.version) < 39) {
        throw Error("daemon protocol %d.%d is too old (< 1.29) to understand build trace",
            GET_PROTOCOL_MAJOR(conn.version),
            GET_PROTOCOL_MINOR(conn.version));
    }

    auto drvPath = ServeProto::Serialise<StorePath>::read(store, conn);
    auto outputName = ServeProto::Serialise<std::string>::read(store, conn);

    return DrvOutput {
        .drvPath = std::move(drvPath),
        .outputName = std::move(outputName),
    };
}

void ServeProto::Serialise<DrvOutput>::write(const StoreDirConfig & store, WriteConn conn, const DrvOutput & info)
{
    if (GET_PROTOCOL_MINOR(conn.version) < 39) {
        throw Error("daemon protocol %d.%d is too old (< 1.29) to understand build trace",
            GET_PROTOCOL_MAJOR(conn.version),
            GET_PROTOCOL_MINOR(conn.version));
    }
    ServeProto::write(store, conn, info.drvPath);
    ServeProto::write(store, conn, info.outputName);
}


Realisation ServeProto::Serialise<Realisation>::read(const StoreDirConfig & store, ReadConn conn)
{
    auto id = ServeProto::Serialise<DrvOutput>::read(store, conn);
    auto unkeyed = ServeProto::Serialise<UnkeyedRealisation>::read(store, conn);

    return Realisation {
        std::move(unkeyed),
        std::move(id),
    };
}

void ServeProto::Serialise<Realisation>::write(const StoreDirConfig & store, WriteConn conn, const Realisation & info)
{
    ServeProto::write(store, conn, info.id);
    ServeProto::write(store, conn, static_cast<const UnkeyedRealisation &>(info));
}

}
