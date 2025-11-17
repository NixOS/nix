#ifndef NIX_API_STORE_STORE_PATH_H
#define NIX_API_STORE_STORE_PATH_H
/**
 * @defgroup libstore_storepath StorePath
 * @ingroup libstore
 * @brief Store path operations that don't require a Store
 * @{
 */
/** @file
 * @brief Store path operations
 */

#include "nix_api_util.h"

#ifdef __cplusplus
extern "C" {
#endif
// cffi start

/** @brief Nix store path */
typedef struct StorePath StorePath;

/**
 * @brief Copy a StorePath
 *
 * @param[in] p the path to copy
 * @return a new StorePath
 */
StorePath * nix_store_path_clone(const StorePath * p);

/** @brief Deallocate a StorePath
 *
 * Does not fail.
 * @param[in] p the path to free
 */
void nix_store_path_free(StorePath * p);

/**
 * @brief Get the path name (e.g. "<name>" in /nix/store/<hash>-<name>)
 *
 * @param[in] store_path the path to get the name from
 * @param[in] callback called with the name
 * @param[in] user_data arbitrary data, passed to the callback when it's called.
 */
void nix_store_path_name(const StorePath * store_path, nix_get_string_callback callback, void * user_data);

// cffi end
#ifdef __cplusplus
}
#endif
/**
 * @}
 */
#endif // NIX_API_STORE_STORE_PATH_H
