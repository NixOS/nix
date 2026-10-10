#ifndef NIX_API_STORE_PATH_INFO_H
#define NIX_API_STORE_PATH_INFO_H
/**
 * @defgroup libstore_pathinfo PathInfo
 * @ingroup libstore
 * @brief Store path metadata
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
 * e.g. "sha256:1b8m03r63zqhnjf7l5nh...". This is the format used in narinfo files.
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
 * @return NAR size in bytes, 0 on error. Note that a NAR always has a root object,
 *         so an actual NAR stream is never empty.
 */
uint64_t nix_path_info_get_nar_size(nix_c_context * context, const nix_path_info * path_info);

/**
 * @brief Iterate over the references of a store path
 *
 * Calls the callback once for each reference. The StorePath passed to the
 * callback is borrowed and only valid for the duration of the callback.
 * Iteration stops if the callback returns with `context` in an error state.
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
    void (*callback)(nix_c_context * context, void * user_data, const StorePath * store_path));

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
 * The `sig` data is borrowed and the callback must not assume that the buffer
 * persists after it returns.
 *
 * Iteration stops if the callback returns with `context` in an error state.
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
    void (*callback)(nix_c_context * context, void * user_data, const char * sig, unsigned int sig_len));

/**
 * @brief Get the content address of a store path, if it has one
 *
 * If so, "returns" the hash as a string with method and algorithm prefix in Nix base-32 encoding,
 * e.g. `"fixed:r:sha256:1i89icvvs2f3cym00414i3bbl1qidhg0b5yrmdlx9cjkj5is6ljg"`.
 *
 * If the store object referenced by `path_info` is not content-addressed,
 * the return code is `NIX_ERR_KEY`, and the callback is not called.
 *
 * `NIX_ERR_KEY` is only returned when `path_info` is not content-addressed.
 *
 * Input-addressed store paths have a content hash (see `nix_path_info_get_nar_hash`,
 * but no content *address*, so that results in NIX_ERR_KEY, distinguishable from
 * other, perhaps more unexpected errors.
 *
 * @param[out] context Optional, stores error information
 * @param[in] path_info the nix_path_info to read from
 * @param[in] callback called with the content address string (only called when present)
 * @param[in] user_data arbitrary data, passed to the callback when it's called
 * @return NIX_OK on success, NIX_ERR_KEY if the path is not content-addressed,
 *         another error code on failure
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
