#include "nix/cmd/develop.hh"

#include "nix_api_develop.h"
#include "nix_api_util_internal.h"
#include "nix_api_store_internal.h"

extern "C" {

nix_err nix_libcmd_get_legacy_shell_derivation_environment(
    nix_c_context * context, Store * store, Store * eval_store, StorePath drv_path, StorePath * out_path)
{
    try {
        *out_path = StorePath{.path = nix::getDerivationEnvironment(store->ptr, eval_store->ptr, drv_path.path)};
    }
    NIXC_CATCH_ERRS
}

} // extern "C"
