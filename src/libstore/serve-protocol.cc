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
    BuildResult::Success success;
    BuildResult::Failure failure;

    auto rawStatus = readInt(conn.from);
    conn.from >> failure.errorMsg;

    if (GET_PROTOCOL_MINOR(conn.version) >= 3)
        conn.from >> status.timesBuilt >> failure.isNonDeterministic >> status.startTime >> status.stopTime;
    if (GET_PROTOCOL_MINOR(conn.version) >= 6) {
        auto builtOutputs = ServeProto::Serialise<DrvOutputs>::read(store, conn);
        for (auto && [output, realisation] : builtOutputs)
            success.builtOutputs.insert_or_assign(std::move(output.outputName), std::move(realisation));
    }

    if (BuildResult::Success::statusIs(rawStatus)) {
        success.status = static_cast<BuildResult::Success::Status>(rawStatus);
        status.inner = std::move(success);
    } else {
        failure.status = static_cast<BuildResult::Failure::Status>(rawStatus);
        status.inner = std::move(failure);
    }

    return status;
}

void ServeProto::Serialise<BuildResult>::write(
    const StoreDirConfig & store, ServeProto::WriteConn conn, const BuildResult & res)
{
    /* The protocol predates the use of sum types (std::variant) to
       separate the success or failure cases. As such, it transits some
       success- or failure-only fields in both cases. This helper
       function helps support this: in each case, we just pass the old
       default value for the fields that don't exist in that case. */
    auto common = [&](std::string_view errorMsg, bool isNonDeterministic, const auto & builtOutputs) {
        conn.to << errorMsg;
        if (GET_PROTOCOL_MINOR(conn.version) >= 3)
            conn.to << res.timesBuilt << isNonDeterministic << res.startTime << res.stopTime;
        if (GET_PROTOCOL_MINOR(conn.version) >= 6) {
            DrvOutputs builtOutputsFullKey;
            for (auto & [output, realisation] : builtOutputs)
                builtOutputsFullKey.insert_or_assign(realisation.id, realisation);
            ServeProto::write(store, conn, builtOutputsFullKey);
        }
    };
    std::visit(
        overloaded{
            [&](const BuildResult::Failure & failure) {
                conn.to << failure.status;
                common(failure.errorMsg, failure.isNonDeterministic, decltype(BuildResult::Success::builtOutputs){});
            },
            [&](const BuildResult::Success & success) {
                conn.to << success.status;
                common(/*errorMsg=*/"", /*isNonDeterministic=*/false, success.builtOutputs);
            },
        },
        res.inner);
}

UnkeyedValidPathInfo ServeProto::Serialise<UnkeyedValidPathInfo>::read(const StoreDirConfig & store, ReadConn conn)
{
    /* Hash should be set below unless very old `nix-store --serve`.
       Caller should assert that it did set it. */
    UnkeyedValidPathInfo info{Hash::dummy};

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

void ServeProto::Serialise<UnkeyedValidPathInfo>::write(
    const StoreDirConfig & store, WriteConn conn, const UnkeyedValidPathInfo & info)
{
    conn.to << (info.deriver ? store.printStorePath(*info.deriver) : "");

    ServeProto::write(store, conn, info.references);
    // !!! Maybe we want compression?
    conn.to << info.narSize // downloadSize, lie a little
            << info.narSize;
    if (GET_PROTOCOL_MINOR(conn.version) >= 4)
        conn.to << info.narHash.to_string(HashFormat::Nix32, true) << renderContentAddress(info.ca) << info.sigs;
}

ServeProto::BuildOptions
ServeProto::Serialise<ServeProto::BuildOptions>::read(const StoreDirConfig & store, ReadConn conn)
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

void ServeProto::Serialise<ServeProto::BuildOptions>::write(
    const StoreDirConfig & store, WriteConn conn, const ServeProto::BuildOptions & options)
{
    conn.to << options.maxSilentTime << options.buildTimeout;
    if (GET_PROTOCOL_MINOR(conn.version) >= 2)
        conn.to << options.maxLogSize;
    if (GET_PROTOCOL_MINOR(conn.version) >= 3)
        conn.to << options.nrRepeats << options.enforceDeterminism;

    if (GET_PROTOCOL_MINOR(conn.version) >= 7) {
        conn.to << ((int) options.keepFailed);
    }
}

} // namespace nix
