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

#include <stddef.h>
#include <stdint.h>

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

/**
 * @brief A store path hash
 *
 * Once decoded from "nix32" encoding, a store path hash is 20 raw bytes.
 */
typedef struct nix_store_path_hash_part
{
    uint8_t bytes[20];
} nix_store_path_hash_part;

/**
 * @brief Get the path hash (e.g. "<hash>" in /nix/store/<hash>-<name>)
 *
 * The hash is returned as raw bytes, decoded from "nix32" encoding.
 *
 * @param[out] context Optional, stores error information
 * @param[in] store_path the path to get the hash from
 * @param[out] hash_part_out the decoded hash as 20 raw bytes
 * @return NIX_OK on success, error code on failure
 */
nix_err
nix_store_path_hash(nix_c_context * context, const StorePath * store_path, nix_store_path_hash_part * hash_part_out);

/**
 * @brief Create a StorePath from its constituent parts (hash and name)
 *
 * This function constructs a store path from a hash and name, without needing
 * a Store reference or the store directory prefix.
 *
 * @note Don't forget to free this path using nix_store_path_free()!
 * @param[out] context Optional, stores error information
 * @param[in] hash The store path hash (20 raw bytes)
 * @param[in] name The store path name (the part after the hash)
 * @param[in] name_len Length of the name string
 * @return owned store path, NULL on error
 */
StorePath * nix_store_create_from_parts(
    nix_c_context * context, const nix_store_path_hash_part * hash, const char name[/*name_len*/], size_t name_len);

// cffi end
#ifdef __cplusplus
}
#endif
/**
 * @}
 */
#endif // NIX_API_STORE_STORE_PATH_H
