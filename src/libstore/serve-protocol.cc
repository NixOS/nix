#include "nix/util/serialise.hh"
#include "nix/store/path-with-outputs.hh"
#include "nix/store/store-api.hh"
#include "nix/store/build-result.hh"
#include "nix/store/common-protocol.hh"
#include "nix/store/serve-protocol.hh"
#include "nix/store/serve-protocol-impl.hh"
#include "nix/util/archive.hh"
#include "nix/store/path-info.hh"
#include "nix/util/json-utils.hh"

#include <nlohmann/json.hpp>

namespace nix {

/* protocol-specific definitions */

BuildResult ServeProto::Serialise<BuildResult>::read(const StoreDirConfig & store, ServeProto::ReadConn conn)
{
    BuildResult res;
    BuildResult::Success success;

    // Temp variables for failure fields since BuildError uses methods
    std::string errorMsg;
    bool isNonDeterministic = false;

    auto status = ServeProto::Serialise<BuildResultStatus>::read(store, {conn.from});
    conn.from >> errorMsg;

    if (conn.version >= ServeProto::Version{2, 3})
        conn.from >> res.timesBuilt >> isNonDeterministic >> res.startTime >> res.stopTime;

    if (conn.version >= ServeProto::Version{2, 8}) {
        success.builtOutputs = ServeProto::Serialise<std::map<OutputName, UnkeyedRealisation>>::read(store, conn);
    } else if (conn.version >= ServeProto::Version{2, 6}) {
        for (auto & [output, realisation] : ServeProto::Serialise<StringMap>::read(store, conn)) {
            size_t n = output.find("!");
            if (n == output.npos)
                throw Error("Invalid derivation output id %s", output);
            success.builtOutputs.insert_or_assign(
                output.substr(n + 1),
                UnkeyedRealisation{
                    StorePath{getString(valueAt(getObject(nlohmann::json::parse(realisation)), "outPath"))}});
        }
    }

    res.inner = std::visit(
        overloaded{
            [&](BuildResult::Success::Status s) -> decltype(res.inner) {
                success.status = s;
                return std::move(success);
            },
            [&](BuildResult::Failure::Status s) -> decltype(res.inner) {
                return BuildResult::Failure{{
                    .status = s,
                    .msg = HintFmt(std::move(errorMsg)),
                    .isNonDeterministic = isNonDeterministic,
                }};
            },
        },
        status);

    return res;
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

        if (conn.version >= ServeProto::Version{2, 3})
            conn.to << res.timesBuilt << isNonDeterministic << res.startTime << res.stopTime;

        if (conn.version >= ServeProto::Version{2, 8}) {
            ServeProto::write(store, conn, builtOutputs);
        } else if (conn.version >= ServeProto::Version{2, 6}) {
            // We no longer support these types of realisations
            ServeProto::write(store, conn, StringMap{});
        }
    };

    std::visit(
        overloaded{
            [&](const BuildResult::Failure & failure) {
                ServeProto::write(store, {conn.to}, BuildResultStatus{failure.status});
                common(failure.message(), failure.isNonDeterministic, decltype(BuildResult::Success::builtOutputs){});
            },
            [&](const BuildResult::Success & success) {
                ServeProto::write(store, {conn.to}, BuildResultStatus{success.status});
                common(/*errorMsg=*/"", /*isNonDeterministic=*/false, success.builtOutputs);
            },
        },
        res.inner);
}

UnkeyedValidPathInfo ServeProto::Serialise<UnkeyedValidPathInfo>::read(const StoreDirConfig & store, ReadConn conn)
{
    /* Hash should be set below unless very old `nix-store --serve`.
       Caller should assert that it did set it. */
    UnkeyedValidPathInfo info{store, Hash::dummy};

    auto deriver = readString(conn.from);
    if (deriver != "")
        info.deriver = store.parseStorePath(deriver);
    info.references = ServeProto::Serialise<StorePathSet>::read(store, conn);

    readLongLong(conn.from); // download size, unused
    info.narSize = readLongLong(conn.from);

    if (conn.version >= ServeProto::Version{2, 4}) {
        auto s = readString(conn.from);
        if (!s.empty())
            info.narHash = Hash::parseAnyPrefixed(s);
        info.ca = ContentAddress::parseOpt(readString(conn.from));
        info.sigs = ServeProto::Serialise<std::set<Signature>>::read(store, conn);
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
    if (conn.version >= ServeProto::Version{2, 4}) {
        conn.to << info.narHash.to_string(HashFormat::Nix32, true) << renderContentAddress(info.ca);
        ServeProto::write(store, conn, info.sigs);
    }
}

ServeProto::BuildOptions
ServeProto::Serialise<ServeProto::BuildOptions>::read(const StoreDirConfig & store, ReadConn conn)
{
    BuildOptions options;
    options.maxSilentTime = readInt(conn.from);
    options.buildTimeout = readInt(conn.from);
    if (conn.version >= ServeProto::Version{2, 2})
        options.maxLogSize = readNum<unsigned long>(conn.from);
    if (conn.version >= ServeProto::Version{2, 3}) {
        options.nrRepeats = readInt(conn.from);
        options.enforceDeterminism = readInt(conn.from);
    }
    if (conn.version >= ServeProto::Version{2, 7}) {
        options.keepFailed = (bool) readInt(conn.from);
    }
    return options;
}

void ServeProto::Serialise<ServeProto::BuildOptions>::write(
    const StoreDirConfig & store, WriteConn conn, const ServeProto::BuildOptions & options)
{
    conn.to << options.maxSilentTime << options.buildTimeout;
    if (conn.version >= ServeProto::Version{2, 2})
        conn.to << options.maxLogSize;
    if (conn.version >= ServeProto::Version{2, 3})
        conn.to << options.nrRepeats << options.enforceDeterminism;

    if (conn.version >= ServeProto::Version{2, 7}) {
        conn.to << ((int) options.keepFailed);
    }
}

UnkeyedRealisation ServeProto::Serialise<UnkeyedRealisation>::read(const StoreDirConfig & store, ReadConn conn)
{
    if (conn.version < ServeProto::Version{2, 8}) {
        throw Error(
            "serve protocol %d.%d is too old (< 2.8) to support content-addressing derivations",
            conn.version.major,
            conn.version.minor);
    }

    auto outPath = ServeProto::Serialise<StorePath>::read(store, conn);
    auto signatures = ServeProto::Serialise<std::set<Signature>>::read(store, conn);

    return UnkeyedRealisation{
        .outPath = std::move(outPath),
        .signatures = std::move(signatures),
    };
}

void ServeProto::Serialise<UnkeyedRealisation>::write(
    const StoreDirConfig & store, WriteConn conn, const UnkeyedRealisation & info)
{
    if (conn.version < ServeProto::Version{2, 8}) {
        throw Error(
            "serve protocol %d.%d is too old (< 2.8) to support content-addressing derivations",
            conn.version.major,
            conn.version.minor);
    }
    ServeProto::write(store, conn, info.outPath);
    ServeProto::write(store, conn, info.signatures);
}

DrvOutput ServeProto::Serialise<DrvOutput>::read(const StoreDirConfig & store, ReadConn conn)
{
    if (conn.version < ServeProto::Version{2, 8}) {
        throw Error(
            "serve protocol %d.%d is too old (< 2.8) to support content-addressing derivations",
            conn.version.major,
            conn.version.minor);
    }

    auto drvPath = ServeProto::Serialise<StorePath>::read(store, conn);
    auto outputName = ServeProto::Serialise<std::string>::read(store, conn);

    return DrvOutput{
        .drvPath = std::move(drvPath),
        .outputName = std::move(outputName),
    };
}

void ServeProto::Serialise<DrvOutput>::write(const StoreDirConfig & store, WriteConn conn, const DrvOutput & info)
{
    if (conn.version < ServeProto::Version{2, 8}) {
        throw Error(
            "serve protocol %d.%d is too old (< 2.8) to support content-addressing derivations",
            conn.version.major,
            conn.version.minor);
    }
    ServeProto::write(store, conn, info.drvPath);
    ServeProto::write(store, conn, info.outputName);
}

Realisation ServeProto::Serialise<Realisation>::read(const StoreDirConfig & store, ReadConn conn)
{
    auto id = ServeProto::Serialise<DrvOutput>::read(store, conn);
    auto unkeyed = ServeProto::Serialise<UnkeyedRealisation>::read(store, conn);

    return Realisation{
        std::move(unkeyed),
        std::move(id),
    };
}

void ServeProto::Serialise<Realisation>::write(const StoreDirConfig & store, WriteConn conn, const Realisation & info)
{
    ServeProto::write(store, conn, info.id);
    ServeProto::write(store, conn, static_cast<const UnkeyedRealisation &>(info));
}

} // namespace nix
