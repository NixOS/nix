#include "nix/util/serialise.hh"
#include "nix/store/path-with-outputs.hh"
#include "nix/store/store-api.hh"
#include "nix/store/build-result.hh"
#include "nix/store/worker-protocol.hh"
#include "nix/store/worker-protocol-impl.hh"
#include "nix/util/archive.hh"
#include "nix/store/path-info.hh"
#include "nix/util/json-utils.hh"

#include <chrono>
#include <nlohmann/json.hpp>

namespace nix {

/* protocol-specific definitions */

BuildMode WorkerProto::Serialise<BuildMode>::read(const StoreDirConfig & store, WorkerProto::ReadConn conn)
{
    auto temp = readNum<uint8_t>(conn.from);
    switch (temp) {
    case 0:
        return bmNormal;
    case 1:
        return bmRepair;
    case 2:
        return bmCheck;
    default:
        throw Error("Invalid build mode");
    }
}

void WorkerProto::Serialise<BuildMode>::write(
    const StoreDirConfig & store, WorkerProto::WriteConn conn, const BuildMode & buildMode)
{
    switch (buildMode) {
    case bmNormal:
        conn.to << uint8_t{0};
        break;
    case bmRepair:
        conn.to << uint8_t{1};
        break;
    case bmCheck:
        conn.to << uint8_t{2};
        break;
    default:
        assert(false);
    };
}

std::optional<TrustedFlag>
WorkerProto::Serialise<std::optional<TrustedFlag>>::read(const StoreDirConfig & store, WorkerProto::ReadConn conn)
{
    auto temp = readNum<uint8_t>(conn.from);
    switch (temp) {
    case 0:
        return std::nullopt;
    case 1:
        return {Trusted};
    case 2:
        return {NotTrusted};
    default:
        throw Error("Invalid trusted status from remote");
    }
}

void WorkerProto::Serialise<std::optional<TrustedFlag>>::write(
    const StoreDirConfig & store, WorkerProto::WriteConn conn, const std::optional<TrustedFlag> & optTrusted)
{
    if (!optTrusted)
        conn.to << uint8_t{0};
    else {
        switch (*optTrusted) {
        case Trusted:
            conn.to << uint8_t{1};
            break;
        case NotTrusted:
            conn.to << uint8_t{2};
            break;
        default:
            assert(false);
        };
    }
}

std::optional<std::chrono::microseconds> WorkerProto::Serialise<std::optional<std::chrono::microseconds>>::read(
    const StoreDirConfig & store, WorkerProto::ReadConn conn)
{
    auto tag = readNum<uint8_t>(conn.from);
    switch (tag) {
    case 0:
        return std::nullopt;
    case 1:
        return std::optional<std::chrono::microseconds>{std::chrono::microseconds(readNum<int64_t>(conn.from))};
    default:
        throw Error("Invalid optional tag from remote");
    }
}

void WorkerProto::Serialise<std::optional<std::chrono::microseconds>>::write(
    const StoreDirConfig & store,
    WorkerProto::WriteConn conn,
    const std::optional<std::chrono::microseconds> & optDuration)
{
    if (!optDuration.has_value()) {
        conn.to << uint8_t{0};
    } else {
        conn.to << uint8_t{1} << optDuration.value().count();
    }
}

DerivedPath WorkerProto::Serialise<DerivedPath>::read(const StoreDirConfig & store, WorkerProto::ReadConn conn)
{
    auto s = readString(conn.from);
    if (GET_PROTOCOL_MINOR(conn.version) >= 30) {
        return DerivedPath::parseLegacy(store, s);
    } else {
        return parsePathWithOutputs(store, s).toDerivedPath();
    }
}

void WorkerProto::Serialise<DerivedPath>::write(
    const StoreDirConfig & store, WorkerProto::WriteConn conn, const DerivedPath & req)
{
    if (GET_PROTOCOL_MINOR(conn.version) >= 30) {
        conn.to << req.to_string_legacy(store);
    } else {
        auto sOrDrvPath = StorePathWithOutputs::tryFromDerivedPath(req);
        std::visit(
            overloaded{
                [&](const StorePathWithOutputs & s) { conn.to << s.to_string(store); },
                [&](const StorePath & drvPath) {
                    throw Error(
                        "trying to request '%s', but daemon protocol %d.%d is too old (< 1.29) to request a derivation file",
                        store.printStorePath(drvPath),
                        GET_PROTOCOL_MAJOR(conn.version) >> 8,
                        GET_PROTOCOL_MINOR(conn.version));
                },
                [&](std::monostate) {
                    throw Error(
                        "wanted to build a derivation that is itself a build product, but protocols do not support that. Try upgrading the Nix on the other end of this connection");
                },
            },
            sOrDrvPath);
    }
}

KeyedBuildResult
WorkerProto::Serialise<KeyedBuildResult>::read(const StoreDirConfig & store, WorkerProto::ReadConn conn)
{
    auto path = WorkerProto::Serialise<DerivedPath>::read(store, conn);
    auto br = WorkerProto::Serialise<BuildResult>::read(store, conn);
    return KeyedBuildResult{
        std::move(br),
        /* .path = */ std::move(path),
    };
}

void WorkerProto::Serialise<KeyedBuildResult>::write(
    const StoreDirConfig & store, WorkerProto::WriteConn conn, const KeyedBuildResult & res)
{
    WorkerProto::write(store, conn, res.path);
    WorkerProto::write(store, conn, static_cast<const BuildResult &>(res));
}

BuildResult WorkerProto::Serialise<BuildResult>::read(const StoreDirConfig & store, WorkerProto::ReadConn conn)
{
    BuildResult res;
    BuildResult::Success success;
    BuildResult::Failure failure;

    auto rawStatus = readInt(conn.from);
    conn.from >> failure.errorMsg;

    if (GET_PROTOCOL_MINOR(conn.version) >= 29) {
        conn.from >> res.timesBuilt >> failure.isNonDeterministic >> res.startTime >> res.stopTime;
    }

    if (GET_PROTOCOL_MINOR(conn.version) >= 37) {
        res.cpuUser = WorkerProto::Serialise<std::optional<std::chrono::microseconds>>::read(store, conn);
        res.cpuSystem = WorkerProto::Serialise<std::optional<std::chrono::microseconds>>::read(store, conn);
    }

    if (GET_PROTOCOL_MINOR(conn.version) >= 39) {
        success.builtOutputs = WorkerProto::Serialise<std::map<OutputName, UnkeyedRealisation>>::read(store, conn);
    } else if (GET_PROTOCOL_MINOR(conn.version) >= 28) {
        for (auto && [output, realisation] : WorkerProto::Serialise<StringMap>::read(store, conn)) {
            size_t n = output.find("!");
            if (n == output.npos)
                throw Error("Invalid derivation output id %s", output);
            success.builtOutputs.insert_or_assign(
                output.substr(n + 1),
                UnkeyedRealisation{
                    StorePath{getString(valueAt(getObject(nlohmann::json::parse(realisation)), "outPath"))}});
        }
    }

    if (BuildResult::Success::statusIs(rawStatus)) {
        success.status = static_cast<BuildResult::Success::Status>(rawStatus);
        res.inner = std::move(success);
    } else {
        failure.status = static_cast<BuildResult::Failure::Status>(rawStatus);
        res.inner = std::move(failure);
    }

    return res;
}

void WorkerProto::Serialise<BuildResult>::write(
    const StoreDirConfig & store, WorkerProto::WriteConn conn, const BuildResult & res)
{
    /* The protocol predates the use of sum types (std::variant) to
       separate the success or failure cases. As such, it transits some
       success- or failure-only fields in both cases. This helper
       function helps support this: in each case, we just pass the old
       default value for the fields that don't exist in that case. */
    auto common = [&](std::string_view errorMsg, bool isNonDeterministic, const auto & builtOutputs) {
        conn.to << errorMsg;

        if (GET_PROTOCOL_MINOR(conn.version) >= 29) {
            conn.to << res.timesBuilt << isNonDeterministic << res.startTime << res.stopTime;
        }

        if (GET_PROTOCOL_MINOR(conn.version) >= 37) {
            WorkerProto::write(store, conn, res.cpuUser);
            WorkerProto::write(store, conn, res.cpuSystem);
        }

        if (GET_PROTOCOL_MINOR(conn.version) >= 39) {
            WorkerProto::write(store, conn, builtOutputs);
        } else if (GET_PROTOCOL_MINOR(conn.version) >= 28) {
            // Don't support those types of realisations anymore.
            WorkerProto::write(store, conn, StringMap{});
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

ValidPathInfo WorkerProto::Serialise<ValidPathInfo>::read(const StoreDirConfig & store, ReadConn conn)
{
    auto path = WorkerProto::Serialise<StorePath>::read(store, conn);
    return ValidPathInfo{
        std::move(path),
        WorkerProto::Serialise<UnkeyedValidPathInfo>::read(store, conn),
    };
}

void WorkerProto::Serialise<ValidPathInfo>::write(
    const StoreDirConfig & store, WriteConn conn, const ValidPathInfo & pathInfo)
{
    WorkerProto::write(store, conn, pathInfo.path);
    WorkerProto::write(store, conn, static_cast<const UnkeyedValidPathInfo &>(pathInfo));
}

UnkeyedValidPathInfo WorkerProto::Serialise<UnkeyedValidPathInfo>::read(const StoreDirConfig & store, ReadConn conn)
{
    auto deriver = WorkerProto::Serialise<std::optional<StorePath>>::read(store, conn);
    auto narHash = Hash::parseAny(readString(conn.from), HashAlgorithm::SHA256);
    UnkeyedValidPathInfo info(narHash);
    info.deriver = std::move(deriver);
    info.references = WorkerProto::Serialise<StorePathSet>::read(store, conn);
    conn.from >> info.registrationTime >> info.narSize;
    if (GET_PROTOCOL_MINOR(conn.version) >= 16) {
        conn.from >> info.ultimate;
        info.sigs = readStrings<StringSet>(conn.from);
        info.ca = ContentAddress::parseOpt(readString(conn.from));
    }
    return info;
}

void WorkerProto::Serialise<UnkeyedValidPathInfo>::write(
    const StoreDirConfig & store, WriteConn conn, const UnkeyedValidPathInfo & pathInfo)
{
    WorkerProto::write(store, conn, pathInfo.deriver);
    conn.to << pathInfo.narHash.to_string(HashFormat::Base16, false);
    WorkerProto::write(store, conn, pathInfo.references);
    conn.to << pathInfo.registrationTime << pathInfo.narSize;
    if (GET_PROTOCOL_MINOR(conn.version) >= 16) {
        conn.to << pathInfo.ultimate << pathInfo.sigs << renderContentAddress(pathInfo.ca);
    }
}

WorkerProto::ClientHandshakeInfo
WorkerProto::Serialise<WorkerProto::ClientHandshakeInfo>::read(const StoreDirConfig & store, ReadConn conn)
{
    WorkerProto::ClientHandshakeInfo res;

    if (GET_PROTOCOL_MINOR(conn.version) >= 33) {
        res.daemonNixVersion = readString(conn.from);
    }

    if (GET_PROTOCOL_MINOR(conn.version) >= 35) {
        res.remoteTrustsUs = WorkerProto::Serialise<std::optional<TrustedFlag>>::read(store, conn);
    } else {
        // We don't know the answer; protocol to old.
        res.remoteTrustsUs = std::nullopt;
    }

    return res;
}

void WorkerProto::Serialise<WorkerProto::ClientHandshakeInfo>::write(
    const StoreDirConfig & store, WriteConn conn, const WorkerProto::ClientHandshakeInfo & info)
{
    if (GET_PROTOCOL_MINOR(conn.version) >= 33) {
        assert(info.daemonNixVersion);
        conn.to << *info.daemonNixVersion;
    }

    if (GET_PROTOCOL_MINOR(conn.version) >= 35) {
        WorkerProto::write(store, conn, info.remoteTrustsUs);
    }
}

UnkeyedRealisation WorkerProto::Serialise<UnkeyedRealisation>::read(const StoreDirConfig & store, ReadConn conn)
{
    if (GET_PROTOCOL_MINOR(conn.version) < 39) {
        throw Error(
            "daemon protocol %d.%d is too old (< 1.39) to understand build trace",
            GET_PROTOCOL_MAJOR(conn.version) >> 8,
            GET_PROTOCOL_MINOR(conn.version));
    }

    auto outPath = WorkerProto::Serialise<StorePath>::read(store, conn);
    auto signatures = WorkerProto::Serialise<StringSet>::read(store, conn);

    return UnkeyedRealisation{
        .outPath = std::move(outPath),
        .signatures = std::move(signatures),
    };
}

void WorkerProto::Serialise<UnkeyedRealisation>::write(
    const StoreDirConfig & store, WriteConn conn, const UnkeyedRealisation & info)
{
    if (GET_PROTOCOL_MINOR(conn.version) < 39) {
        throw Error(
            "daemon protocol %d.%d is too old (< 1.39) to understand build trace",
            GET_PROTOCOL_MAJOR(conn.version) >> 8,
            GET_PROTOCOL_MINOR(conn.version));
    }
    WorkerProto::write(store, conn, info.outPath);
    WorkerProto::write(store, conn, info.signatures);
}

std::optional<UnkeyedRealisation>
WorkerProto::Serialise<std::optional<UnkeyedRealisation>>::read(const StoreDirConfig & store, ReadConn conn)
{
    if (GET_PROTOCOL_MINOR(conn.version) < 39) {
        // Hack to improve compat
        (void) WorkerProto::Serialise<std::string>::read(store, conn);
        return std::nullopt;
    } else {
        auto temp = readNum<uint8_t>(conn.from);
        switch (temp) {
        case 0:
            return std::nullopt;
        case 1:
            return WorkerProto::Serialise<UnkeyedRealisation>::read(store, conn);
        default:
            throw Error("Invalid optional build trace from remote");
        }
    }
}

void WorkerProto::Serialise<std::optional<UnkeyedRealisation>>::write(
    const StoreDirConfig & store, WriteConn conn, const std::optional<UnkeyedRealisation> & info)
{
    if (!info) {
        conn.to << uint8_t{0};
    } else {
        conn.to << uint8_t{1};
        WorkerProto::write(store, conn, *info);
    }
}

DrvOutput WorkerProto::Serialise<DrvOutput>::read(const StoreDirConfig & store, ReadConn conn)
{
    if (GET_PROTOCOL_MINOR(conn.version) < 39) {
        throw Error(
            "daemon protocol %d.%d is too old (< 1.29) to understand build trace",
            GET_PROTOCOL_MAJOR(conn.version) >> 8,
            GET_PROTOCOL_MINOR(conn.version));
    }

    auto drvPath = WorkerProto::Serialise<StorePath>::read(store, conn);
    auto outputName = WorkerProto::Serialise<std::string>::read(store, conn);

    return DrvOutput{
        .drvPath = std::move(drvPath),
        .outputName = std::move(outputName),
    };
}

void WorkerProto::Serialise<DrvOutput>::write(const StoreDirConfig & store, WriteConn conn, const DrvOutput & info)
{
    if (GET_PROTOCOL_MINOR(conn.version) < 39) {
        throw Error(
            "daemon protocol %d.%d is too old (< 1.29) to understand build trace",
            GET_PROTOCOL_MAJOR(conn.version) >> 8,
            GET_PROTOCOL_MINOR(conn.version));
    }
    WorkerProto::write(store, conn, info.drvPath);
    WorkerProto::write(store, conn, info.outputName);
}

Realisation WorkerProto::Serialise<Realisation>::read(const StoreDirConfig & store, ReadConn conn)
{
    auto id = WorkerProto::Serialise<DrvOutput>::read(store, conn);
    auto unkeyed = WorkerProto::Serialise<UnkeyedRealisation>::read(store, conn);

    return Realisation{
        std::move(unkeyed),
        std::move(id),
    };
}

void WorkerProto::Serialise<Realisation>::write(const StoreDirConfig & store, WriteConn conn, const Realisation & info)
{
    WorkerProto::write(store, conn, info.id);
    WorkerProto::write(store, conn, static_cast<const UnkeyedRealisation &>(info));
}

} // namespace nix
