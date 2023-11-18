#include "serialise.hh"
#include "path-with-outputs.hh"
#include "store-api.hh"
#include "build-result.hh"
#include "serve-protocol.hh"
#include "serve-protocol-impl.hh"
#include "archive.hh"

#include <nlohmann/json.hpp>

namespace nix {

/* protocol-specific definitions */

BuildResult ServeProto::Serialise<BuildResult>::read(const Store & store, ServeProto::ReadConn conn)
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

void ServeProto::Serialise<BuildResult>::write(const Store & store, ServeProto::WriteConn conn, const BuildResult & status)
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

}
