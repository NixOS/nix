#include "nix/util/serialise.hh"
#include "nix/store/path-with-outputs.hh"
#include "nix/store/store-api.hh"
#include "nix/store/gc-store.hh"
#include "nix/store/build-result.hh"
#include "nix/store/common-protocol.hh"
#include "nix/store/worker-protocol.hh"
#include "nix/store/worker-protocol-impl.hh"
#include "nix/util/archive.hh"
#include "nix/store/path-info.hh"

#include <chrono>
#include <nlohmann/json.hpp>

namespace nix {

const WorkerProto::Version WorkerProto::latest = {
    .number =
        {
            .major = 1,
            .minor = 38,
        },
    .features =
        {
            FEATURE_GC_CLOSURE,
        },
};

const WorkerProto::Version WorkerProto::minimum = {
    .number =
        {
            .major = 1,
            .minor = 18,
        },
};

std::partial_ordering WorkerProto::Version::operator<=>(const WorkerProto::Version & other) const
{
    auto numCmp = number <=> other.number;
    bool thisSubsetEq = std::includes(other.features.begin(), other.features.end(), features.begin(), features.end());
    bool otherSubsetEq = std::includes(features.begin(), features.end(), other.features.begin(), other.features.end());

    if (numCmp == 0 && thisSubsetEq && otherSubsetEq)
        return std::partial_ordering::equivalent;
    if (numCmp <= 0 && thisSubsetEq)
        return std::partial_ordering::less;
    if (numCmp >= 0 && otherSubsetEq)
        return std::partial_ordering::greater;
    return std::partial_ordering::unordered;
}

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

GCAction WorkerProto::Serialise<GCAction>::read(const StoreDirConfig & store, WorkerProto::ReadConn conn)
{
    auto temp = readNum<unsigned>(conn.from);
    using enum GCAction;
    switch (temp) {
    case 0:
        return gcReturnLive;
    case 1:
        return gcReturnDead;
    case 2:
        return gcDeleteDead;
    case 3:
        return gcDeleteSpecific;
    default:
        throw Error("Invalid GC action");
    }
}

void WorkerProto::Serialise<GCAction>::write(
    const StoreDirConfig & store, WorkerProto::WriteConn conn, const GCAction & action)
{
    using enum GCAction;
    switch (action) {
    case gcReturnLive:
        conn.to << unsigned{0};
        break;
    case gcReturnDead:
        conn.to << unsigned{1};
        break;
    case gcDeleteDead:
        conn.to << unsigned{2};
        break;
    case gcDeleteSpecific:
        conn.to << unsigned{3};
        break;
    default:
        assert(false);
    }
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
    if (conn.version >= WorkerProto::Version{.number = {1, 30}}) {
        return DerivedPath::parseLegacy(store, s);
    } else {
        return parsePathWithOutputs(store, s).toDerivedPath();
    }
}

void WorkerProto::Serialise<DerivedPath>::write(
    const StoreDirConfig & store, WorkerProto::WriteConn conn, const DerivedPath & req)
{
    if (conn.version >= WorkerProto::Version{.number = {1, 30}}) {
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
                        conn.version.number.major,
                        conn.version.number.minor);
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

    // Temp variables for failure fields since BuildError uses methods
    std::string errorMsg;
    bool isNonDeterministic = false;

    auto status = WorkerProto::Serialise<BuildResultStatus>::read(store, conn);
    conn.from >> errorMsg;

    if (conn.version >= WorkerProto::Version{.number = {1, 29}}) {
        conn.from >> res.timesBuilt >> isNonDeterministic >> res.startTime >> res.stopTime;
    }
    if (conn.version >= WorkerProto::Version{.number = {1, 37}}) {
        res.cpuUser = WorkerProto::Serialise<std::optional<std::chrono::microseconds>>::read(store, conn);
        res.cpuSystem = WorkerProto::Serialise<std::optional<std::chrono::microseconds>>::read(store, conn);
    }
    if (conn.version >= WorkerProto::Version{.number = {1, 28}}) {
        auto builtOutputs = WorkerProto::Serialise<DrvOutputs>::read(store, conn);
        for (auto && [output, realisation] : builtOutputs)
            success.builtOutputs.insert_or_assign(std::move(output.outputName), std::move(realisation));
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
        if (conn.version >= WorkerProto::Version{.number = {1, 29}}) {
            conn.to << res.timesBuilt << isNonDeterministic << res.startTime << res.stopTime;
        }
        if (conn.version >= WorkerProto::Version{.number = {1, 37}}) {
            WorkerProto::write(store, conn, res.cpuUser);
            WorkerProto::write(store, conn, res.cpuSystem);
        }
        if (conn.version >= WorkerProto::Version{.number = {1, 28}}) {
            DrvOutputs builtOutputsFullKey;
            for (auto & [output, realisation] : builtOutputs)
                builtOutputsFullKey.insert_or_assign(realisation.id, realisation);
            WorkerProto::write(store, conn, builtOutputsFullKey);
        }
    };
    std::visit(
        overloaded{
            [&](const BuildResult::Failure & failure) {
                WorkerProto::write(store, conn, BuildResultStatus{failure.status});
                common(failure.message(), failure.isNonDeterministic, decltype(BuildResult::Success::builtOutputs){});
            },
            [&](const BuildResult::Success & success) {
                WorkerProto::write(store, conn, BuildResultStatus{success.status});
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
    UnkeyedValidPathInfo info(store, narHash);
    info.deriver = std::move(deriver);
    info.references = WorkerProto::Serialise<StorePathSet>::read(store, conn);
    conn.from >> info.registrationTime >> info.narSize;
    if (conn.version >= WorkerProto::Version{.number = {1, 16}}) {
        conn.from >> info.ultimate;
        info.sigs = WorkerProto::Serialise<std::set<Signature>>::read(store, conn);
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
    if (conn.version >= WorkerProto::Version{.number = {1, 16}}) {
        conn.to << pathInfo.ultimate;
        WorkerProto::write(store, conn, pathInfo.sigs);
        conn.to << renderContentAddress(pathInfo.ca);
    }
}

WorkerProto::ClientHandshakeInfo
WorkerProto::Serialise<WorkerProto::ClientHandshakeInfo>::read(const StoreDirConfig & store, ReadConn conn)
{
    WorkerProto::ClientHandshakeInfo res;

    if (conn.version >= WorkerProto::Version{.number = {1, 33}}) {
        res.daemonNixVersion = readString(conn.from);
    }

    if (conn.version >= WorkerProto::Version{.number = {1, 35}}) {
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
    if (conn.version >= WorkerProto::Version{.number = {1, 33}}) {
        assert(info.daemonNixVersion);
        conn.to << *info.daemonNixVersion;
    }

    if (conn.version >= WorkerProto::Version{.number = {1, 35}}) {
        WorkerProto::write(store, conn, info.remoteTrustsUs);
    }
}

} // namespace nix
