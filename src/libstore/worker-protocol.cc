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
namespace worker_proto {

/* protocol-specific definitions */

BuildResult read(const Store & store, ReadConn conn, Phantom<BuildResult> _)
{
    auto path = worker_proto::read(store, conn, Phantom<DerivedPath> {});
    BuildResult res { .path = path };
    res.status = (BuildResult::Status) readInt(conn.from);
    conn.from
        >> res.errorMsg
        >> res.timesBuilt
        >> res.isNonDeterministic
        >> res.startTime
        >> res.stopTime;
    res.builtOutputs = read(store, conn, Phantom<DrvOutputs> {});
    return res;
}

void write(const Store & store, WriteConn conn, const BuildResult & res)
{
    worker_proto::write(store, conn, res.path);
    conn.to
        << res.status
        << res.errorMsg
        << res.timesBuilt
        << res.isNonDeterministic
        << res.startTime
        << res.stopTime;
    write(store, conn, res.builtOutputs);
}


}
}
