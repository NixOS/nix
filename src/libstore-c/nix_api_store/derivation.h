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
 * @brief Deallocate a `nix_derivation`
 *
 * Does not fail.
 * @param[in] drv the derivation to free
 */
void nix_derivation_free(nix_derivation * drv);

// cffi end
#ifdef __cplusplus
}
#endif
/**
 * @}
 */
#endif // NIX_API_STORE_DERIVATION_H
