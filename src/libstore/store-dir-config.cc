#include "nix/store/store-dir-config.hh"
#include "nix/util/util.hh"
#include "nix/store/globals.hh"

namespace nix {

StoreDirConfig::StoreDirConfig(const Params & params)
    : StoreDirConfigItself(params)
    , MixStoreDirMethods{storeDir_}
{
}

}
