#ifndef NIX_API_FLAKE_H
#define NIX_API_FLAKE_H
/** @defgroup libflake libflake
 * @brief Bindings to the Nix Flakes library
 *
 * @{
 */
/** @file
 * @brief Main entry for the libflake C bindings
 */

#include "nix_api_fetchers.h"
#include "nix_api_store.h"
#include "nix_api_util.h"
#include "nix_api_expr.h"

#ifdef __cplusplus
extern "C" {
#endif
// cffi start

/**
 * @brief A settings object for configuring the behavior of the nix-flake-c library.
 * @see nix_flake_settings_new
 * @see nix_flake_settings_free
 */
typedef struct nix_flake_settings nix_flake_settings;

/**
 * @brief Context and parameters for parsing a flake reference
 * @see nix_flake_reference_parse_flags_free
 * @see nix_flake_reference_parse_string
 */
typedef struct nix_flake_reference_parse_flags nix_flake_reference_parse_flags;

/**
 * @brief A reference to a flake
 *
 * A flake reference specifies how to fetch a flake.
 *
 * @see nix_flake_reference_from_string
 * @see nix_flake_reference_free
 */
typedef struct nix_flake_reference nix_flake_reference;

/**
 * @brief Parameters for locking a flake
 * @see nix_flake_lock_flags_new
 * @see nix_flake_lock_flags_free
 * @see nix_flake_lock
 */
typedef struct nix_flake_lock_flags nix_flake_lock_flags;

/**
 * @brief A flake with a suitable lock (file or otherwise)
 * @see nix_flake_lock
 * @see nix_locked_flake_free
 * @see nix_locked_flake_get_output_attrs
 */
typedef struct nix_locked_flake nix_locked_flake;

// Function prototypes
/**
 * Create a nix_flake_settings initialized with default values.
 * @param[out] context Optional, stores error information
 * @return A new nix_flake_settings or NULL on failure.
 * @see nix_flake_settings_free
 */
nix_flake_settings * nix_flake_settings_new(nix_c_context * context);

/**
 * @brief Release the resources associated with a nix_flake_settings.
 */
void nix_flake_settings_free(nix_flake_settings * settings);

/**
 * @brief Initialize a `nix_flake_settings` to contain `builtins.getFlake` and
 * potentially more.
 *
 * @warning This does not put the eval state in pure mode!
 *
 * @param[out] context Optional, stores error information
 * @param[in] settings The settings to use for e.g. `builtins.getFlake`
 * @param[in] builder The builder to modify
 */
nix_err nix_flake_settings_add_to_eval_state_builder(
    nix_c_context * context, nix_flake_settings * settings, nix_eval_state_builder * builder);

/**
 * @brief A new `nix_flake_reference_parse_flags` with defaults
 */
nix_flake_reference_parse_flags *
nix_flake_reference_parse_flags_new(nix_c_context * context, nix_flake_settings * settings);

/**
 * @brief Deallocate and release the resources associated with a `nix_flake_reference_parse_flags`.
 * Does not fail.
 * @param[in] flags the `nix_flake_reference_parse_flags *` to free
 */
void nix_flake_reference_parse_flags_free(nix_flake_reference_parse_flags * flags);

/**
 * @brief Provide a base directory for parsing relative flake references
 * @param[out] context Optional, stores error information
 * @param[in] flags The flags to modify
 * @param[in] baseDirectory The base directory to add
 * @param[in] baseDirectoryLen The length of baseDirectory
 * @return NIX_OK on success, NIX_ERR on failure
 */
nix_err nix_flake_reference_parse_flags_set_base_directory(
    nix_c_context * context,
    nix_flake_reference_parse_flags * flags,
    const char * baseDirectory,
    size_t baseDirectoryLen);

/**
 * @brief A new `nix_flake_lock_flags` with defaults
 * @param[in] settings Flake settings that may affect the defaults
 */
nix_flake_lock_flags * nix_flake_lock_flags_new(nix_c_context * context, nix_flake_settings * settings);

/**
 * @brief Deallocate and release the resources associated with a `nix_flake_lock_flags`.
 * Does not fail.
 * @param[in] settings the `nix_flake_lock_flags *` to free
 */
void nix_flake_lock_flags_free(nix_flake_lock_flags * settings);

/**
 * @brief Put the lock flags in a mode that checks whether the lock is up to date.
 * @param[out] context Optional, stores error information
 * @param[in] flags The flags to modify
 * @return NIX_OK on success, NIX_ERR on failure
 *
 * This causes `nix_flake_lock` to fail if the lock needs to be updated.
 */
nix_err nix_flake_lock_flags_set_mode_check(nix_c_context * context, nix_flake_lock_flags * flags);

/**
 * @brief Put the lock flags in a mode that updates the lock file in memory, if needed.
 * @param[out] context Optional, stores error information
 * @param[in] flags The flags to modify
 * @param[in] update Whether to allow updates
 *
 * This will cause `nix_flake_lock` to update the lock file in memory, if needed.
 */
nix_err nix_flake_lock_flags_set_mode_virtual(nix_c_context * context, nix_flake_lock_flags * flags);

/**
 * @brief Put the lock flags in a mode that updates the lock file on disk, if needed.
 * @param[out] context Optional, stores error information
 * @param[in] flags The flags to modify
 * @param[in] update Whether to allow updates
 *
 * This will cause `nix_flake_lock` to update the lock file on disk, if needed.
 */
nix_err nix_flake_lock_flags_set_mode_write_as_needed(nix_c_context * context, nix_flake_lock_flags * flags);

/**
 * @brief Add input overrides to the lock flags
 * @param[out] context Optional, stores error information
 * @param[in] flags The flags to modify
 * @param[in] inputPath The input path to override
 * @param[in] flakeRef The flake reference to use as the override
 *
 * This switches the `flags` to `nix_flake_lock_flags_set_mode_virtual` if not in mode
 * `nix_flake_lock_flags_set_mode_check`.
 */
nix_err nix_flake_lock_flags_add_input_override(
    nix_c_context * context, nix_flake_lock_flags * flags, const char * inputPath, nix_flake_reference * flakeRef);

/**
 * @brief Lock a flake, if not already locked.
 * @param[out] context Optional, stores error information
 * @param[in] settings The flake (and fetch) settings to use
 * @param[in] flags The locking flags to use
 * @param[in] flake The flake to lock
 */
nix_locked_flake * nix_flake_lock(
    nix_c_context * context,
    nix_fetchers_settings * fetchSettings,
    nix_flake_settings * settings,
    EvalState * eval_state,
    nix_flake_lock_flags * flags,
    nix_flake_reference * flake);

/**
 * @brief Deallocate and release the resources associated with a `nix_locked_flake`.
 * Does not fail.
 * @param[in] locked_flake the `nix_locked_flake *` to free
 */
void nix_locked_flake_free(nix_locked_flake * locked_flake);

/**
 * @brief Parse a URL-like string into a `nix_flake_reference`.
 *
 * @param[out] context **context** – Optional, stores error information
 * @param[in] fetchSettings **context** – The fetch settings to use
 * @param[in] flakeSettings **context** – The flake settings to use
 * @param[in] parseFlags **context** – Specific context and parameters such as base directory
 *
 * @param[in] str **input** – The URI-like string to parse
 * @param[in] strLen **input** – The length of `str`
 *
 * @param[out] flakeReferenceOut **result** – The resulting flake reference
 * @param[in] fragmentCallback **result** – A callback to call with the fragment part of the URL
 * @param[in] fragmentCallbackUserData **result** – User data to pass to the fragment callback
 *
 * @return NIX_OK on success, NIX_ERR on failure
 */
nix_err nix_flake_reference_and_fragment_from_string(
    nix_c_context * context,
    nix_fetchers_settings * fetchSettings,
    nix_flake_settings * flakeSettings,
    nix_flake_reference_parse_flags * parseFlags,
    const char * str,
    size_t strLen,
    nix_flake_reference ** flakeReferenceOut,
    nix_get_string_callback fragmentCallback,
    void * fragmentCallbackUserData);

/**
 * @brief Deallocate and release the resources associated with a `nix_flake_reference`.
 *
 * Does not fail.
 *
 * @param[in] store the `nix_flake_reference *` to free
 */
void nix_flake_reference_free(nix_flake_reference * store);

/**
 * @brief Get the output attributes of a flake.
 * @param[out] context Optional, stores error information
 * @param[in] settings The settings to use
 * @param[in] locked_flake the flake to get the output attributes from
 * @return A new nix_value or NULL on failure. Release the `nix_value` with `nix_value_decref`.
 */
nix_value * nix_locked_flake_get_output_attrs(
    nix_c_context * context, nix_flake_settings * settings, EvalState * evalState, nix_locked_flake * lockedFlake);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
