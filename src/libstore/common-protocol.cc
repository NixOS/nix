#include "serialise.hh"
#include "util.hh"
#include "path-with-outputs.hh"
#include "store-api.hh"
#include "build-result.hh"
#include "common-protocol.hh"
#include "common-protocol-impl.hh"
#include "archive.hh"
#include "derivations.hh"

#include <nlohmann/json.hpp>

namespace nix {
namespace common_proto {

/* protocol-agnostic definitions */
#include "gen-protocol.cc-inc"

}
}
