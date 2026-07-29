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
#include "nix_api_store/fwd.h"
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
 * For each output, the callback receives the output name and the
 * canonical drv-output identifier in the form
 * `<drvPath>^<outputName>`, where `<drvPath>` is the full
 * store-dir-prefixed path passed in as `drv_path`. The returned id
 * round-trips through Nix's drv-output parser and is suitable for
 * passing to `nix_store_query_realisation`.
 *
 * @note The callback may set an error on `context` to abort iteration
 * early; the surrounding call returns that error code.
 *
 * @param[out] context Optional, stores error information
 * @param[in] store nix store reference (used to render `drv_output_id`)
 * @param[in] drv The derivation
 * @param[in] drv_path The store path of the derivation itself. The
 *            `nix_derivation` value does not carry its own path, so the
 *            caller must supply it. This is typically the same path
 *            that was passed to `nix_store_drv_from_store_path`.
 * @param[in] userdata Arbitrary data passed to the callback
 * @param[in] callback Invoked once per output, in unspecified order.
 *            The string pointers are borrowed for the duration of the
 *            call only.
 */
nix_err nix_derivation_get_outputs(
    nix_c_context * context,
    Store * store,
    const nix_derivation * drv,
    const StorePath * drv_path,
    void * userdata,
    void (*callback)(nix_c_context * context, void * userdata, const char * output_name, const char * drv_output_id));

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
 * @param[in] store nix store reference (used to render `input_drv_path`)
 * @param[in] drv The derivation
 * @param[in] userdata Arbitrary data passed to the callback
 * @param[in] callback Invoked once per `(input_drv_path, output_name)`
 *            pair, in unspecified order. The string pointers are
 *            borrowed for the duration of the call only.
 */
nix_err nix_derivation_get_input_drv_outputs(
    nix_c_context * context,
    Store * store,
    const nix_derivation * drv,
    void * userdata,
    void (*callback)(nix_c_context * context, void * userdata, const char * input_drv_path, const char * output_name));

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
