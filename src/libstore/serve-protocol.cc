#include "serialise.hh"
#include "util.hh"
#include "path-with-outputs.hh"
#include "store-api.hh"
#include "build-result.hh"
#include "serve-protocol.hh"
#include "serve-protocol-impl.hh"
#include "archive.hh"
#include "path-info.hh"

#include <nlohmann/json.hpp>

namespace nix {
namespace serve_proto {

/* protocol-specific definitions */

BuildResult read(const Store & store, ReadConn conn, Phantom<BuildResult> _)
{
    if (GET_PROTOCOL_MINOR(conn.version) < 6) {
        BuildResult status;
        status.status = (BuildResult::Status) readInt(conn.from);
        conn.from >> status.errorMsg;

        if (GET_PROTOCOL_MINOR(conn.version) >= 3)
            conn.from >> status.timesBuilt >> status.isNonDeterministic >> status.startTime >> status.stopTime;
        return status;
    } else
        return serve_proto::read0(store, conn, Phantom<BuildResult> {});
}

void write(const Store & store, WriteConn conn, const BuildResult & status)
{
    if (GET_PROTOCOL_MINOR(conn.version < 6)) {
        conn.to << status.status << status.errorMsg;

        if (GET_PROTOCOL_MINOR(conn.version) >= 3)
            conn.to << status.timesBuilt << status.isNonDeterministic << status.startTime << status.stopTime;
    } else {
        serve_proto::write0(store, conn, status);
    }
}

}
}
