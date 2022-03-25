#pragma once

#include "serve-protocol-impl.hh"

namespace nix {
namespace serve_proto {

using namespace common_proto;

/* protocol-agnostic templates */
#include "gen-protocol-templates.hh"

/* protocol-specific templates */

}
}
