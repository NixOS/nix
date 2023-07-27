#ifndef NIX_API_STORE_H
#define NIX_API_STORE_H
/** @file
 * @brief Main entry for the libstore C bindings
 */

#include "nix_api_util.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif
// cffi start

/** @brief reference to a nix store */
typedef struct Store Store;
/** @brief nix store path */
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
nix_err nix_libstore_init(nix_c_context *context);

/**
 * @brief Open a nix store
 * @param[out] context Optional, stores error information
 * @param[in] uri URI of the nix store, copied
 * @param[in] params optional, array of key-value pairs, {{"endpoint",
 * "https://s3.local"}}
 * @return ref-counted Store pointer, NULL in case of errors
 * @see nix_store_unref
 */
Store *nix_store_open(nix_c_context *, const char *uri, const char ***params);

/**
 * @brief Unref a nix store
 *
 * Does not fail.
 * It'll be closed and deallocated when all references are gone.
 * @param[in] builder the store to unref
 */
void nix_store_unref(Store *store);

/**
 * @brief get the URI of a nix store
 * @param[out] context Optional, stores error information
 * @param[in] store nix store reference
 * @param[out] dest The allocated area to write the string to.
 * @param[in] n Maximum size of the returned string.
 * @return error code, NIX_OK on success.
 */
nix_err nix_store_get_uri(nix_c_context *context, Store *store, char *dest,
                          unsigned int n);

// returns: owned StorePath*
/**
 * @brief parse a nix store path into a StorePath
 *
 * Don't forget to free this path using nix_store_path_free
 * @param[out] context Optional, stores error information
 * @param[in] store nix store reference
 * @param[in] path Path string to parse, copied
 * @return owned store path, NULL on error
 */
StorePath *nix_store_parse_path(nix_c_context *context, Store *store,
                                const char *path);

/** @brief Deallocate a nix StorePath
 *
 * Does not fail.
 * @param[in] p the path to free
 */
void nix_store_path_free(StorePath *p);

/**
 * @brief check if a storepath is valid (exists in the store)
 * @param[out] context Optional, stores error information
 * @param[in] store nix store reference
 * @param[in] path Path to check
 * @return true or false, error info in context
 */
bool nix_store_is_valid_path(nix_c_context *context, Store *store,
                             StorePath *path);
// nix_err nix_store_ensure(Store*, const char*);
// nix_err nix_store_build_paths(Store*);
/**
 * @brief Build a nix store path
 *
 * Blocking, calls cb once for each built output
 *
 * @param[out] context Optional, stores error information
 * @param[in] store nix store reference
 * @param[in] path Path to build
 * @param[in] cb called for every built output
 */
nix_err nix_store_build(nix_c_context *context, Store *store, StorePath *path,
                        void (*cb)(const char *outname, const char *out));

/**
 * @brief get the version of a nix store
 * @param[out] context Optional, stores error information
 * @param[in] store nix store reference
 * @param[out] dest The allocated area to write the string to.
 * @param[in] n Maximum size of the returned string.
 * @return error code, NIX_OK on success.
 */
nix_err nix_store_get_version(nix_c_context *, Store *store, char *dest,
                              unsigned int n);

// cffi end
#ifdef __cplusplus
}
#endif

#endif // NIX_API_STORE_H
