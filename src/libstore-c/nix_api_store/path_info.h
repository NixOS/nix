#ifndef NIX_API_STORE_PATH_INFO_H
#define NIX_API_STORE_PATH_INFO_H
/**
 * @defgroup libstore_pathinfo PathInfo
 * @ingroup libstore
 * @brief Store path metadata (narHash, references, signatures, etc.)
 * @{
 */
/** @file
 * @brief Path info operations for querying store object metadata
 */

#include <stdint.h>

#include "nix_api_util.h"
#include "nix_api_store/store_path.h"

#ifdef __cplusplus
extern "C" {
#endif
// cffi start

/** @brief Opaque handle to store path metadata */
typedef struct nix_path_info nix_path_info;

/**
 * @brief Deallocate a nix_path_info
 *
 * Does not fail.
 * @param[in] path_info the nix_path_info to free
 */
void nix_path_info_free(nix_path_info * path_info);

/**
 * @brief Get the NAR hash of a store path
 *
 * Returns the hash as a string with algorithm prefix in Nix base-32 encoding,
 * e.g. "sha256:1b8m03r63zqhnjf7l5nh...". This is the format used in NARINFO files.
 *
 * @param[out] context Optional, stores error information
 * @param[in] path_info the nix_path_info to read from
 * @param[in] callback called with the hash string
 * @param[in] user_data arbitrary data, passed to the callback when it's called
 * @return NIX_OK on success, error code on failure
 */
nix_err nix_path_info_get_nar_hash(
    nix_c_context * context, const nix_path_info * path_info, nix_get_string_callback callback, void * user_data);

/**
 * @brief Get the NAR size of a store path
 *
 * @param[out] context Optional, stores error information
 * @param[in] path_info the nix_path_info to read from
 * @return NAR size in bytes, 0 if unknown
 */
uint64_t nix_path_info_get_nar_size(nix_c_context * context, const nix_path_info * path_info);

/**
 * @brief Iterate over the references of a store path
 *
 * Calls the callback once for each reference. The StorePath passed to the
 * callback is borrowed and only valid for the duration of the callback.
 *
 * @param[out] context Optional, stores error information
 * @param[in] path_info the nix_path_info to read from
 * @param[in] user_data arbitrary data, passed to the callback
 * @param[in] callback called for each referenced store path
 * @return NIX_OK on success, error code on failure
 */
nix_err nix_path_info_get_references(
    nix_c_context * context,
    const nix_path_info * path_info,
    void * user_data,
    void (*callback)(void * user_data, const StorePath * store_path));

/**
 * @brief Get the deriver of a store path
 *
 * @note Don't forget to free the result with nix_store_path_free()!
 * @param[out] context Optional, stores error information
 * @param[in] path_info the nix_path_info to read from
 * @return owned StorePath of the deriver, or NULL if no deriver is known
 */
StorePath * nix_path_info_get_deriver(nix_c_context * context, const nix_path_info * path_info);

/**
 * @brief Iterate over the signatures of a store path
 *
 * Calls the callback once for each signature string (format: "keyName:base64sig").
 *
 * @param[out] context Optional, stores error information
 * @param[in] path_info the nix_path_info to read from
 * @param[in] user_data arbitrary data, passed to the callback
 * @param[in] callback called for each signature string
 * @return NIX_OK on success, error code on failure
 */
nix_err nix_path_info_get_sigs(
    nix_c_context * context,
    const nix_path_info * path_info,
    void * user_data,
    void (*callback)(void * user_data, const char * sig, unsigned int sig_len));

/**
 * @brief Get the content address of a store path, if any
 *
 * If the path is content-addressed, calls the callback with the rendered
 * content address string. If not content-addressed, the callback is not called.
 *
 * @param[out] context Optional, stores error information
 * @param[in] path_info the nix_path_info to read from
 * @param[in] callback called with the content address string, if present
 * @param[in] user_data arbitrary data, passed to the callback when it's called
 * @return NIX_OK on success, error code on failure
 */
nix_err nix_path_info_get_ca(
    nix_c_context * context, const nix_path_info * path_info, nix_get_string_callback callback, void * user_data);

// cffi end
#ifdef __cplusplus
}
#endif
/**
 * @}
 */
#endif // NIX_API_STORE_PATH_INFO_H
