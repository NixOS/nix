#ifndef NIX_API_FLAKE_H
#define NIX_API_FLAKE_H
/** @defgroup libflake libflake
 * @brief Bindings to the Nix Flakes library
 *
 * @{
 */
/** @file
 * @brief Main entry for the libflake C bindings
 */

#include "nix_api_store.h"
#include "nix_api_util.h"
#include "nix_api_expr.h"

#ifdef __cplusplus
extern "C" {
#endif
// cffi start

typedef struct nix_flake_settings nix_flake_settings;

// Function prototypes
/**
 * Create a nix_flake_settings initialized with default values.
 * @param[out] context Optional, stores error information
 * @return A new nix_flake_settings or NULL on failure.
 * @see nix_flake_settings_free
 */
nix_flake_settings * nix_flake_settings_new(nix_c_context * context);

/**
 * @brief Release the resources associated with a nix_flake_settings.
 */
void nix_flake_settings_free(nix_flake_settings * settings);

/**
 * @brief Register Flakes support process-wide.
 */
nix_err nix_flake_init_global(nix_c_context * context, nix_flake_settings * settings);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
