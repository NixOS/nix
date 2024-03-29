#ifndef NIX_API_STORE_H
#define NIX_API_STORE_H
/**
 * @defgroup libstore libstore
 * @brief C bindings for nix libstore
 *
 * libstore is used for talking to a Nix store
 * @{
 */
/** @file
 * @brief Main entry for the libstore C bindings
 */

#include "nix_api_util.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif
// cffi start

/** @brief Reference to a Nix store */
typedef struct Store Store;
/** @brief Nix store path */
typedef struct StorePath StorePath;

/**
 * @brief Initializes the Nix store library
 *
 * This function should be called before creating a Store
 * This function can be called multiple times.
 *
 * @param[out] context Optional, stores error information
 * @return NIX_OK if the initialization was successful, an error code otherwise.
 */
nix_err nix_libstore_init(nix_c_context * context);

/**
 * @brief Loads the plugins specified in Nix's plugin-files setting.
 *
 * Call this once, after calling your desired init functions and setting
 * relevant settings.
 *
 * @param[out] context Optional, stores error information
 * @return NIX_OK if the initialization was successful, an error code otherwise.
 */
nix_err nix_init_plugins(nix_c_context * context);

/**
 * @brief Open a nix store
 * Store instances may share state and resources behind the scenes.
 * @param[out] context Optional, stores error information
 * @param[in] uri URI of the nix store, copied
 * @param[in] params optional, array of key-value pairs, {{"endpoint",
 * "https://s3.local"}}
 * @return a Store pointer, NULL in case of errors
 * @see nix_store_free
 */
Store * nix_store_open(nix_c_context *, const char * uri, const char *** params);

/**
 * @brief Deallocate a nix store and free any resources if not also held by other Store instances.
 *
 * Does not fail.
 *
 * @param[in] store the store to free
 */
void nix_store_free(Store * store);

/**
 * @brief get the URI of a nix store
 * @param[out] context Optional, stores error information
 * @param[in] store nix store reference
 * @param[in] callback Called with the URI.
 * @param[in] user_data optional, arbitrary data, passed to the callback when it's called.
 * @see nix_get_string_callback
 * @return error code, NIX_OK on success.
 */
nix_err nix_store_get_uri(nix_c_context * context, Store * store, void * callback, void * user_data);

// returns: owned StorePath*
/**
 * @brief Parse a Nix store path into a StorePath
 *
 * @note Don't forget to free this path using nix_store_path_free()!
 * @param[out] context Optional, stores error information
 * @param[in] store nix store reference
 * @param[in] path Path string to parse, copied
 * @return owned store path, NULL on error
 */
StorePath * nix_store_parse_path(nix_c_context * context, Store * store, const char * path);

/** @brief Deallocate a StorePath
 *
 * Does not fail.
 * @param[in] p the path to free
 */
void nix_store_path_free(StorePath * p);

/**
 * @brief Check if a StorePath is valid (i.e. that corresponding store object and its closure of references exists in
 * the store)
 * @param[out] context Optional, stores error information
 * @param[in] store Nix Store reference
 * @param[in] path Path to check
 * @return true or false, error info in context
 */
bool nix_store_is_valid_path(nix_c_context * context, Store * store, StorePath * path);
// nix_err nix_store_ensure(Store*, const char*);
// nix_err nix_store_build_paths(Store*);
/**
 * @brief Realise a Nix store path
 *
 * Blocking, calls callback once for each realised output
 *
 * @param[out] context Optional, stores error information
 * @param[in] store Nix Store reference
 * @param[in] path Path to build
 * @param[in] userdata data to pass to every callback invocation
 * @param[in] callback called for every realised output
 */
nix_err nix_store_realise(
    nix_c_context * context,
    Store * store,
    StorePath * path,
    void * userdata,
    void (*callback)(void * userdata, const char * outname, const char * out));

/**
 * @brief get the version of a nix store.
 * If the store doesn't have a version (like the dummy store), returns an empty string.
 * @param[out] context Optional, stores error information
 * @param[in] store nix store reference
 * @param[in] callback Called with the version.
 * @param[in] user_data optional, arbitrary data, passed to the callback when it's called.
 * @see nix_get_string_callback
 * @return error code, NIX_OK on success.
 */
nix_err nix_store_get_version(nix_c_context * context, Store * store, void * callback, void * user_data);

// cffi end
#ifdef __cplusplus
}
#endif
/**
 * @}
 */
#endif // NIX_API_STORE_H
