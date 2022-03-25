#pragma once

#include "worker-protocol-impl.hh"

namespace nix {
namespace worker_proto {

using namespace common_proto;

/* protocol-agnostic templates */
#include "gen-protocol-templates.hh"

/* protocol-specific templates */

}
}
