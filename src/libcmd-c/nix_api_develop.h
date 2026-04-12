#ifndef NIX_API_REPL_H
#define NIX_API_REPL_H
/** @defgroup libcmd libcmd
 * @brief Bindings to the Nix command utilities
 *
 * @{
 */
/** @file
 * @brief Main entry for the libcmd C bindings
 */

#include "nix_api_store.h"
#include "nix_api_util.h"

#ifdef __cplusplus
extern "C" {
#endif
// cffi start

/**
 * Given an existing derivation, return the shell environment as
 * initialised by stdenv's setup script. We do this by building a
 * modified derivation with the same dependencies and nearly the same
 * initial environment variables, that just writes the resulting
 * environment to a file and exits.
 *
 * @param[in] store Store to use for building the environment.
 * @param[in] eval_store Store to use for evaluation.
 * @param[in] drv_path The derivation to compute the shell environment of.
 * @param[out] out_path The built shell environment.
 * @return NIX_OK if the shell environment built successfully, an error code otherwise.
 */
nix_err nix_libcmd_get_legacy_shell_derivation_environment(
    nix_c_context * context, Store * store, Store * eval_store, StorePath drv_path, StorePath * out_path);

// cffi end
#ifdef __cplusplus
}
#endif

/** @} */
#endif
