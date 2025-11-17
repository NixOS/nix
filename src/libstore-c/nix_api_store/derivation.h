#ifndef NIX_API_STORE_DERIVATION_H
#define NIX_API_STORE_DERIVATION_H
/**
 * @defgroup libstore_derivation Derivation
 * @ingroup libstore
 * @brief Derivation operations that don't require a Store
 * @{
 */
/** @file
 * @brief Derivation operations
 */

#include "nix_api_util.h"

#ifdef __cplusplus
extern "C" {
#endif
// cffi start

/** @brief Nix Derivation */
typedef struct nix_derivation nix_derivation;

/**
 * @brief Copy a `nix_derivation`
 *
 * @param[in] d the derivation to copy
 * @return a new `nix_derivation`
 */
nix_derivation * nix_derivation_clone(const nix_derivation * d);

/**
 * @brief Deallocate a `nix_derivation`
 *
 * Does not fail.
 * @param[in] drv the derivation to free
 */
void nix_derivation_free(nix_derivation * drv);

/**
 * @brief Gets the derivation as a JSON string
 *
 * @param[out] context Optional, stores error information
 * @param[in] drv The derivation
 * @param[in] callback Called with the JSON string
 * @param[in] userdata Arbitrary data passed to the callback
 */
nix_err nix_derivation_to_json(
    nix_c_context * context, const nix_derivation * drv, nix_get_string_callback callback, void * userdata);

// cffi end
#ifdef __cplusplus
}
#endif
/**
 * @}
 */
#endif // NIX_API_STORE_DERIVATION_H
