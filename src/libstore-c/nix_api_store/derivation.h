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

#include <stdbool.h>

#include "nix_api_util.h"
#include "nix_api_store/store_path.h"

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

/**
 * @brief Enumerate the outputs of a derivation.
 *
 * @note The callback may set an error on `context` to abort iteration
 * early; the surrounding call returns that error code.
 *
 * @param[out] context Optional, stores error information
 * @param[in] drv The derivation
 * @param[in] userdata Arbitrary data passed to the callback
 * @param[in] callback Required, invoked once per output, in unspecified order.
 *            The output name is borrowed for the duration of the call
 *            only.
 * @return NIX_OK on success, or an error code if callback is NULL or
 *         reports an error through `context`.
 */
nix_err nix_derivation_get_outputs(
    nix_c_context * context,
    const nix_derivation * drv,
    void * userdata,
    void (*callback)(nix_c_context * context, void * userdata, const char * output_name));

/**
 * @brief Enumerate the (input derivation, output name) pairs that this
 * derivation directly consumes.
 *
 * Only the static portion of `inputDrvs` is surfaced. Inputs produced
 * by dynamic derivations (i.e. derivations themselves built by another
 * derivation in the input graph) are ignored; use
 * `nix_derivation_has_dynamic_inputs` to detect that case.
 *
 * @note The callback may set an error on `context` to abort iteration
 * early; the surrounding call returns that error code.
 *
 * @param[out] context Optional, stores error information
 * @param[in] drv The derivation
 * @param[in] userdata Arbitrary data passed to the callback
 * @param[in] callback Required, invoked once per
 *            `(input_drv_path, output_name)` pair, in unspecified order.
 *            The path and output name are borrowed for the duration of
 *            the call only.
 * @return NIX_OK on success, or an error code if callback is NULL or
 *         reports an error through `context`.
 */
nix_err nix_derivation_get_input_drv_outputs(
    nix_c_context * context,
    const nix_derivation * drv,
    void * userdata,
    void (*callback)(
        nix_c_context * context, void * userdata, const StorePath * input_drv_path, const char * output_name));

/**
 * @brief Report whether this derivation has any inputs that are
 * outputs of dynamic derivations.
 *
 * Callers using `nix_derivation_get_input_drv_outputs` should check
 * this and either handle the dynamic case themselves or fail with a
 * clear error: dynamic-derivation inputs are not surfaced by the
 * static enumeration.
 *
 * @param[out] context Optional, stores error information
 * @param[in] drv The derivation
 * @param[out] out_has_dynamic Required, must not be NULL. Set to true
 *             iff at least one input of `drv` is the output of a
 *             dynamic derivation.
 * @return NIX_OK on success, or an error code if `out_has_dynamic` is
 *         NULL.
 */
nix_err nix_derivation_has_dynamic_inputs(nix_c_context * context, const nix_derivation * drv, bool * out_has_dynamic);

// cffi end
#ifdef __cplusplus
}
#endif
/**
 * @}
 */
#endif // NIX_API_STORE_DERIVATION_H
