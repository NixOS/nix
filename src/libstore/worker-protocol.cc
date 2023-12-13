#include "globals.hh"
#include "granular-access-store.hh"
#include "serialise.hh"
#include "path-with-outputs.hh"
#include "store-api.hh"
#include "build-result.hh"
#include "worker-protocol.hh"
#include "worker-protocol-impl.hh"
#include "archive.hh"
#include "path-info.hh"

#include <nlohmann/json.hpp>

namespace nix {

/* protocol-specific definitions */

std::optional<TrustedFlag> WorkerProto::Serialise<std::optional<TrustedFlag>>::read(const StoreDirConfig & store, WorkerProto::ReadConn conn)
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

void WorkerProto::Serialise<std::optional<TrustedFlag>>::write(const StoreDirConfig & store, WorkerProto::WriteConn conn, const std::optional<TrustedFlag> & optTrusted)
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

AuthenticatedUser WorkerProto::Serialise<AuthenticatedUser>::read(const StoreDirConfig & store, WorkerProto::ReadConn conn) {
    AuthenticatedUser user;
    user.trusted = *WorkerProto::Serialise<std::optional<TrustedFlag>>::read(store, conn);
    conn.from >> user.uid;
    return user;
}

void WorkerProto::Serialise<AuthenticatedUser>::write(const StoreDirConfig & store, WorkerProto::WriteConn conn, const AuthenticatedUser & user)
{
    WorkerProto::Serialise<std::optional<TrustedFlag>>::write(store, conn, user.trusted);
    conn.to << user.uid;
}

ACL::User WorkerProto::Serialise<ACL::User>::read(const StoreDirConfig & store, WorkerProto::ReadConn conn) {
    uid_t uid;
    conn.from >> uid;
    return uid;
}

void WorkerProto::Serialise<ACL::User>::write(const StoreDirConfig & store, WorkerProto::WriteConn conn, const ACL::User & user)
{
    conn.to << user.uid;
}

ACL::Group WorkerProto::Serialise<ACL::Group>::read(const StoreDirConfig & store, WorkerProto::ReadConn conn) {
    gid_t gid;
    conn.from >> gid;
    return gid;
}

void WorkerProto::Serialise<ACL::Group>::write(const StoreDirConfig & store, WorkerProto::WriteConn conn, const ACL::Group & group)
{
    conn.to << group.gid;
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

void WorkerProto::Serialise<DerivedPath>::write(const StoreDirConfig & store, WorkerProto::WriteConn conn, const DerivedPath & req)
{
    if (GET_PROTOCOL_MINOR(conn.version) >= 30) {
        conn.to << req.to_string_legacy(store);
    } else {
        auto sOrDrvPath = StorePathWithOutputs::tryFromDerivedPath(req);
        std::visit(overloaded {
            [&](const StorePathWithOutputs & s) {
                conn.to << s.to_string(store);
            },
            [&](const StorePath & drvPath) {
                throw Error("trying to request '%s', but daemon protocol %d.%d is too old (< 1.29) to request a derivation file",
                    store.printStorePath(drvPath),
                    GET_PROTOCOL_MAJOR(conn.version),
                    GET_PROTOCOL_MINOR(conn.version));
            },
            [&](std::monostate) {
                throw Error("wanted to build a derivation that is itself a build product, but protocols do not support that. Try upgrading the Nix on the other end of this connection");
            },
        }, sOrDrvPath);
    }
}

StoreObjectDerivationOutput WorkerProto::Serialise<StoreObjectDerivationOutput>::read(const StoreDirConfig & store, WorkerProto::ReadConn conn)
{
    auto drvPath = WorkerProto::Serialise<StorePath>::read(store, conn);
    auto output = WorkerProto::Serialise<std::string>::read(store, conn);
    return {drvPath, output};
}

void WorkerProto::Serialise<StoreObjectDerivationOutput>::write(const StoreDirConfig & store, WorkerProto::WriteConn conn, const StoreObjectDerivationOutput & drvOutput)
{
    WorkerProto::Serialise<StorePath>::write(store, conn, drvOutput.drvPath);
    WorkerProto::Serialise<std::string>::write(store, conn, drvOutput.output);
}

StoreObjectDerivationLog WorkerProto::Serialise<StoreObjectDerivationLog>::read(const StoreDirConfig & store, WorkerProto::ReadConn conn)
{
    return { WorkerProto::Serialise<StorePath>::read(store, conn) };
}

void WorkerProto::Serialise<StoreObjectDerivationLog>::write(const StoreDirConfig & store, WorkerProto::WriteConn conn, const StoreObjectDerivationLog & drvLog)
{
    WorkerProto::Serialise<StorePath>::write(store, conn, drvLog.drvPath);
}

KeyedBuildResult WorkerProto::Serialise<KeyedBuildResult>::read(const StoreDirConfig & store, WorkerProto::ReadConn conn)
{
    auto path = WorkerProto::Serialise<DerivedPath>::read(store, conn);
    auto br = WorkerProto::Serialise<BuildResult>::read(store, conn);
    return KeyedBuildResult {
        std::move(br),
        /* .path = */ std::move(path),
    };
}

void WorkerProto::Serialise<KeyedBuildResult>::write(const StoreDirConfig & store, WorkerProto::WriteConn conn, const KeyedBuildResult & res)
{
    WorkerProto::write(store, conn, res.path);
    WorkerProto::write(store, conn, static_cast<const BuildResult &>(res));
}


BuildResult WorkerProto::Serialise<BuildResult>::read(const StoreDirConfig & store, WorkerProto::ReadConn conn)
{
    BuildResult res;
    res.status = static_cast<BuildResult::Status>(readInt(conn.from));
    conn.from >> res.errorMsg;
    if (GET_PROTOCOL_MINOR(conn.version) >= 29) {
        conn.from
            >> res.timesBuilt
            >> res.isNonDeterministic
            >> res.startTime
            >> res.stopTime;
    }
    if (GET_PROTOCOL_MINOR(conn.version) >= 28) {
        auto builtOutputs = WorkerProto::Serialise<DrvOutputs>::read(store, conn);
        for (auto && [output, realisation] : builtOutputs)
            res.builtOutputs.insert_or_assign(
                std::move(output.outputName),
                std::move(realisation));
    }
    return res;
}

void WorkerProto::Serialise<BuildResult>::write(const StoreDirConfig & store, WorkerProto::WriteConn conn, const BuildResult & res)
{
    conn.to
        << res.status
        << res.errorMsg;
    if (GET_PROTOCOL_MINOR(conn.version) >= 29) {
        conn.to
            << res.timesBuilt
            << res.isNonDeterministic
            << res.startTime
            << res.stopTime;
    }
    if (GET_PROTOCOL_MINOR(conn.version) >= 28) {
        DrvOutputs builtOutputs;
        for (auto & [output, realisation] : res.builtOutputs)
            builtOutputs.insert_or_assign(realisation.id, realisation);
        WorkerProto::write(store, conn, builtOutputs);
    }
}


ValidPathInfo WorkerProto::Serialise<ValidPathInfo>::read(const StoreDirConfig & store, ReadConn conn)
{
    auto path = WorkerProto::Serialise<StorePath>::read(store, conn);
    return ValidPathInfo {
        std::move(path),
        WorkerProto::Serialise<UnkeyedValidPathInfo>::read(store, conn),
    };
}

void WorkerProto::Serialise<ValidPathInfo>::write(const StoreDirConfig & store, WriteConn conn, const ValidPathInfo & pathInfo)
{
    WorkerProto::write(store, conn, pathInfo.path);
    WorkerProto::write(store, conn, static_cast<const UnkeyedValidPathInfo &>(pathInfo));
}


UnkeyedValidPathInfo WorkerProto::Serialise<UnkeyedValidPathInfo>::read(const StoreDirConfig & store, ReadConn conn)
{
    auto deriver = readString(conn.from);
    auto narHash = Hash::parseAny(readString(conn.from), HashAlgorithm::SHA256);
    UnkeyedValidPathInfo info(narHash);
    if (deriver != "") info.deriver = store.parseStorePath(deriver);
    info.references = WorkerProto::Serialise<StorePathSet>::read(store, conn);
    conn.from >> info.registrationTime >> info.narSize;
    if (GET_PROTOCOL_MINOR(conn.version) >= 16) {
        conn.from >> info.ultimate;
        info.sigs = readStrings<StringSet>(conn.from);
        info.ca = ContentAddress::parseOpt(readString(conn.from));
    }
    return info;
}

void WorkerProto::Serialise<UnkeyedValidPathInfo>::write(const StoreDirConfig & store, WriteConn conn, const UnkeyedValidPathInfo & pathInfo)
{
    conn.to
        << (pathInfo.deriver ? store.printStorePath(*pathInfo.deriver) : "")
        << pathInfo.narHash.to_string(HashFormat::Base16, false);
    WorkerProto::write(store, conn, pathInfo.references);
    conn.to << pathInfo.registrationTime << pathInfo.narSize;
    if (GET_PROTOCOL_MINOR(conn.version) >= 16) {
        conn.to
            << pathInfo.ultimate
            << pathInfo.sigs
            << renderContentAddress(pathInfo.ca);
    }
}

}
