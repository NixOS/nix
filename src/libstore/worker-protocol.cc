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

/* protocol-agnostic definitions */
#include "gen-protocol.cc-inc"

/* protocol-specific definitions */

BuildResult read(const Store & store, Source & from, Phantom<BuildResult> _)
{
    auto path = worker_proto::read(store, from, Phantom<DerivedPath> {});
    BuildResult res { .path = path };
    res.status = (BuildResult::Status) readInt(from);
    from
        >> res.errorMsg
        >> res.timesBuilt
        >> res.isNonDeterministic
        >> res.startTime
        >> res.stopTime;
    res.builtOutputs = worker_proto::read(store, from, Phantom<DrvOutputs> {});
    return res;
}

void write(const Store & store, Sink & to, const BuildResult & res)
{
    worker_proto::write(store, to, res.path);
    to
        << res.status
        << res.errorMsg
        << res.timesBuilt
        << res.isNonDeterministic
        << res.startTime
        << res.stopTime;
    worker_proto::write(store, to, res.builtOutputs);
}


}
}
