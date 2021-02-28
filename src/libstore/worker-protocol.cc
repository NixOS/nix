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

/* protocol-specific definitions */

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


DerivedPath WorkerProto::Serialise<DerivedPath>::read(const Store & store, WorkerProto::ReadConn conn)
{
    auto s = readString(conn.from);
    if (GET_PROTOCOL_MINOR(conn.version) >= 30) {
        return DerivedPath::parseLegacy(store, s);
    } else {
        return parsePathWithOutputs(store, s).toDerivedPath();
    }
}

void WorkerProto::Serialise<DerivedPath>::write(const Store & store, WorkerProto::WriteConn conn, const DerivedPath & req)
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

void WorkerProto::Serialise<BuildResult>::write(const Store & store, WorkerProto::WriteConn conn, const BuildResult & res)
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


}
