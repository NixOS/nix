/// Runtime versions of the third-parties libraries libexpr adds on top of the
/// lower nix libraries' dependencies.

#include "nix/util/library-versions.hh"
#include "nix/util/fmt.hh"

#include "nix/expr/config.hh"

#if NIX_USE_BOEHMGC
#  include <gc/gc.h>
#endif

namespace nix {

#if NIX_USE_BOEHMGC
static std::string libgcVersion()
{
    unsigned v = GC_get_version();
    // This is the documented method.
    return fmt("%d.%d.%d", (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
}

static RegisterLibraryVersion rLibgc{"libgc", libgcVersion};
#endif

} // namespace nix
