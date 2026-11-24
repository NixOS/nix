#ifndef NIX_API_EVAL_CACHE_H
#define NIX_API_EVAL_CACHE_H
/** @addtogroup libflake
 * @{
 */
/** @file
 * @brief Bindings to the evaluation cache of a locked flake
 *
 * A cursor walks the output attributes of a locked flake the way the `nix`
 * command does: attributes already in the on-disk cache are read from there,
 * the rest are evaluated and cached. A flake whose source is not locked has no
 * cache and is always evaluated.
 */

#include "nix_api_util.h"
#include "nix_api_expr.h"
#include "nix_api_flake.h"

#ifdef __cplusplus
extern "C" {
#endif
// cffi start

/**
 * @brief An open evaluation cache for a locked flake.
 * @see nix_eval_cache_open
 * @see nix_eval_cache_free
 */
typedef struct nix_eval_cache nix_eval_cache;

/**
 * @brief A cursor into the attribute tree of an evaluation cache.
 * @see nix_eval_cache_get_root
 * @see nix_attr_cursor_free
 */
typedef struct nix_attr_cursor nix_attr_cursor;

/**
 * @brief Open the evaluation cache for a locked flake.
 * @param[out] context Optional, stores error information
 * @param[in] state The eval state to evaluate the flake in
 * @param[in] locked_flake The locked flake to open the cache for
 * @return A new nix_eval_cache or NULL on failure. Free with nix_eval_cache_free.
 */
nix_eval_cache * nix_eval_cache_open(nix_c_context * context, EvalState * state, nix_locked_flake * locked_flake);

/**
 * @brief Release the resources associated with a nix_eval_cache. Does not fail.
 */
void nix_eval_cache_free(nix_eval_cache * cache);

/**
 * @brief Get a cursor pointing at the root of the cache's attribute tree.
 * @param[out] context Optional, stores error information
 * @param[in] cache The evaluation cache
 * @return A new nix_attr_cursor or NULL on failure. Free with nix_attr_cursor_free.
 */
nix_attr_cursor * nix_eval_cache_get_root(nix_c_context * context, nix_eval_cache * cache);

/**
 * @brief Fold the cache's WAL into the main `.sqlite` file (PASSIVE checkpoint).
 *
 * Makes the writes so far visible to a reader of the database file alone
 * (without its `-wal` sidecar). Does not block on concurrent connections.
 * @param[out] context Optional, stores error information
 * @param[in] cache The evaluation cache
 * @return NIX_OK on success.
 */
nix_err nix_eval_cache_checkpoint(nix_c_context * context, nix_eval_cache * cache);

/**
 * @brief Descend into a child attribute by name.
 * @param[out] context Optional, stores error information
 * @param[in] cursor The parent cursor
 * @param[in] name The attribute name to look up
 * @return A new nix_attr_cursor, or NULL if the attribute is missing or on
 * failure. Check the context to distinguish a missing attribute from an error.
 * Free the result with nix_attr_cursor_free.
 */
nix_attr_cursor * nix_attr_cursor_maybe_get_attr(nix_c_context * context, nix_attr_cursor * cursor, const char * name);

/**
 * @brief Release the resources associated with a nix_attr_cursor. Does not fail.
 */
void nix_attr_cursor_free(nix_attr_cursor * cursor);

/**
 * @brief Enumerate the attribute names of the attrset at the cursor.
 * @param[out] context Optional, stores error information
 * @param[in] state The eval state, used to resolve symbols to names
 * @param[in] cursor The cursor, which must point at an attrset
 * @param[in] callback Called once per attribute name
 * @param[in] user_data Optional, passed to the callback
 * @return NIX_OK on success.
 */
nix_err nix_attr_cursor_get_attrs(
    nix_c_context * context,
    EvalState * state,
    nix_attr_cursor * cursor,
    void (*callback)(const char * name, void * user_data),
    void * user_data);

/**
 * @brief Determine whether the cursor points at a derivation.
 * @param[out] context Optional, stores error information
 * @param[in] cursor The cursor
 * @param[out] out Set to whether the attrset is a derivation
 * @return NIX_OK on success.
 */
nix_err nix_attr_cursor_is_derivation(nix_c_context * context, nix_attr_cursor * cursor, bool * out);

/**
 * @brief Force the derivation at the cursor and report its `.drv` store path.
 * @param[out] context Optional, stores error information
 * @param[in] state The eval state, whose store is used to print the path
 * @param[in] cursor The cursor, which must point at a derivation
 * @param[in] callback Called with the store path string
 * @param[in] user_data Optional, passed to the callback
 * @return NIX_OK on success.
 */
nix_err nix_attr_cursor_get_drv_path(
    nix_c_context * context,
    EvalState * state,
    nix_attr_cursor * cursor,
    nix_get_string_callback callback,
    void * user_data);

/**
 * @brief Read the string value at the cursor.
 * @param[out] context Optional, stores error information
 * @param[in] cursor The cursor, which must point at a string
 * @param[in] callback Called with the string value
 * @param[in] user_data Optional, passed to the callback
 * @return NIX_OK on success.
 */
nix_err nix_attr_cursor_get_string(
    nix_c_context * context, nix_attr_cursor * cursor, nix_get_string_callback callback, void * user_data);

/**
 * @brief Read the boolean value at the cursor.
 * @param[out] context Optional, stores error information
 * @param[in] cursor The cursor, which must point at a bool
 * @param[out] out Set to the boolean value
 * @return NIX_OK on success.
 */
nix_err nix_attr_cursor_get_bool(nix_c_context * context, nix_attr_cursor * cursor, bool * out);

/**
 * @brief Read a list of strings at the cursor.
 * @param[out] context Optional, stores error information
 * @param[in] cursor The cursor, which must point at a list of strings
 * @param[in] callback Called once per list element
 * @param[in] user_data Optional, passed to the callback
 * @return NIX_OK on success.
 */
nix_err nix_attr_cursor_get_list_of_strings(
    nix_c_context * context,
    nix_attr_cursor * cursor,
    void (*callback)(const char * value, void * user_data),
    void * user_data);

/**
 * @brief Evaluate the attribute at the cursor and return its value.
 *
 * For a value the cache cannot hold, such as an attrset or a function.
 * @param[out] context Optional, stores error information
 * @param[in] state The eval state that owns the value
 * @param[in] cursor The cursor
 * @return The forced value, or NULL on failure. Release with nix_value_decref.
 */
nix_value * nix_attr_cursor_force_value(nix_c_context * context, EvalState * state, nix_attr_cursor * cursor);

// cffi end
#ifdef __cplusplus
} // extern "C"
#endif
/**
 * @}
 */
#endif // NIX_API_EVAL_CACHE_H
