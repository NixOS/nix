#ifndef NIX_API_FETCHERS_H
#define NIX_API_FETCHERS_H
/** @defgroup libfetchers libfetchers
 * @brief Bindings to the Nix fetchers library
 * @{
 */
/** @file
 * @brief Main entry for the libfetchers C bindings
 */

#include "nix_api_util.h"

#ifdef __cplusplus
extern "C" {
#endif
// cffi start

// Type definitions
/**
 * @brief Shared settings object
 */
typedef struct nix_fetchers_settings nix_fetchers_settings;

nix_fetchers_settings * nix_fetchers_settings_new(nix_c_context * context);

void nix_fetchers_settings_free(nix_fetchers_settings * settings);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // NIX_API_FETCHERS_H