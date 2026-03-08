#ifndef NIX_API_STORE_PATH_INFO_H
#define NIX_API_STORE_PATH_INFO_H
/**
 * @defgroup libstore_pathinfo PathInfo
 * @ingroup libstore
 * @brief Query information about store paths
 * @{
 */
/** @file
 * @brief Path info operations
 */

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "nix_api_util.h"
#include "nix_api_store/store_path.h"

#ifdef __cplusplus
extern "C" {
#endif
// cffi start

/** @brief Information about a valid store path */
typedef struct ValidPathInfo ValidPathInfo;

/**
 * @brief Deallocate a ValidPathInfo
 *
 * Does not fail.
 * @param[in] info the path info to free
 */
void nix_valid_path_info_free(ValidPathInfo * info);

/**
 * @brief Get the references of a store path
 *
 * References are other store paths that this path depends on.
 *
 * @note The callback borrows each StorePath only for the duration of the call.
 *
 * @param[out] context Optional, stores error information
 * @param[in] info The path info
 * @param[in] userdata The userdata to pass to the callback
 * @param[in] callback The function to call for every reference
 * @return NIX_OK on success, error code on failure
 */
nix_err nix_valid_path_info_get_references(
    nix_c_context * context,
    const ValidPathInfo * info,
    void * userdata,
    void (*callback)(void * userdata, const StorePath * reference));

/**
 * @brief Get the deriver of a store path
 *
 * The deriver is the .drv file that was used to build this path.
 *
 * @param[out] context Optional, stores error information
 * @param[in] info The path info
 * @return The deriver store path, or NULL if there is no deriver. Caller must free with nix_store_path_free.
 */
StorePath * nix_valid_path_info_get_deriver(nix_c_context * context, const ValidPathInfo * info);

/**
 * @brief Get the NAR hash of a store path
 *
 * The NAR hash is the hash of the NAR (Nix ARchive) serialization of the path's contents.
 *
 * @param[out] context Optional, stores error information
 * @param[in] info The path info
 * @param[in] callback Called with the NAR hash in SRI format (e.g., "sha256-...")
 * @param[in] user_data optional, arbitrary data, passed to the callback when it's called.
 * @return NIX_OK on success, error code on failure
 */
nix_err nix_valid_path_info_get_nar_hash(
    nix_c_context * context, const ValidPathInfo * info, nix_get_string_callback callback, void * user_data);

/**
 * @brief Get the NAR size of a store path
 *
 * The NAR size is the size in bytes of the NAR serialization of the path's contents.
 *
 * @param[in] info The path info
 * @return The NAR size in bytes, or 0 if unknown
 */
uint64_t nix_valid_path_info_get_nar_size(const ValidPathInfo * info);

/**
 * @brief Get the registration time of a store path
 *
 * This is when the path was registered in the store.
 *
 * @param[in] info The path info
 * @return The registration time as a Unix timestamp, or 0 if unknown
 */
time_t nix_valid_path_info_get_registration_time(const ValidPathInfo * info);

// cffi end
#ifdef __cplusplus
}
#endif
/**
 * @}
 */
#endif // NIX_API_STORE_PATH_INFO_H
