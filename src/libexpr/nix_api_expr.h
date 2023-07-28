#ifndef NIX_API_EXPR_H
#define NIX_API_EXPR_H
/** @file
 * @brief Main entry for the libexpr C bindings
 */

#include "nix_api_store.h"
#include "nix_api_util.h"

#ifdef __cplusplus
extern "C" {
#endif
// cffi start

// Type definitions
/**
 * @brief Represents a nix evaluator state.
 *
 * Multiple can be created for multi-threaded
 * operation.
 */
typedef struct State State; // nix::EvalState
/**
 * @brief Represents a nix value.
 *
 * Owned by the GC.
 */
typedef void Value; // nix::Value

// Function prototypes
/**
 * @brief Initializes the Nix expression evaluator.
 *
 * This function should be called before creating a State.
 * This function can be called multiple times.
 *
 * @param[out] context Optional, stores error information
 * @return NIX_OK if the initialization was successful, an error code otherwise.
 */
nix_err nix_libexpr_init(nix_c_context *context);

/**
 * @brief Parses and evaluates a Nix expression from a string.
 *
 * @param[out] context Optional, stores error information
 * @param[in] state The state of the evaluation.
 * @param[in] expr The Nix expression to parse.
 * @param[in] path The file path to associate with the expression.
 * @param[out] value The result of the evaluation. You should allocate this
 * yourself.
 * @return NIX_OK if the evaluation was successful, an error code otherwise.
 */
nix_err nix_expr_eval_from_string(nix_c_context *context, State *state,
                                  const char *expr, const char *path,
                                  Value *value);

/**
 * @brief Calls a Nix function with an argument.
 *
 * @param[out] context Optional, stores error information
 * @param[in] state The state of the evaluation.
 * @param[in] fn The Nix function to call.
 * @param[in] arg The argument to pass to the function.
 * @param[out] value The result of the function call.
 * @return NIX_OK if the function call was successful, an error code otherwise.
 */
nix_err nix_value_call(nix_c_context *context, State *state, Value *fn,
                       Value *arg, Value *value);

/**
 * @brief Forces the evaluation of a Nix value.
 *
 * @param[out] context Optional, stores error information
 * @param[in] state The state of the evaluation.
 * @param[in,out] value The Nix value to force.
 * @return NIX_OK if the force operation was successful, an error code
 * otherwise.
 */
nix_err nix_value_force(nix_c_context *context, State *state, Value *value);

/**
 * @brief Forces the deep evaluation of a Nix value.
 *
 * @param[out] context Optional, stores error information
 * @param[in] state The state of the evaluation.
 * @param[in,out] value The Nix value to force.
 * @return NIX_OK if the deep force operation was successful, an error code
 * otherwise.
 */
nix_err nix_value_force_deep(nix_c_context *context, State *state,
                             Value *value);

/**
 * @brief Creates a new Nix state.
 *
 * @param[out] context Optional, stores error information
 * @param[in] searchPath The NIX_PATH.
 * @param[in] store The Nix store to use.
 * @return A new Nix state or NULL on failure.
 */
State *nix_state_create(nix_c_context *context, const char **searchPath,
                        Store *store);

/**
 * @brief Frees a Nix state.
 *
 * Does not fail.
 *
 * @param[in] state The state to free.
 */
void nix_state_free(State *state);

/**
 * @brief Increase the GC refcount.
 *
 * The nix C api keeps alive objects by refcounting.
 * When you're done with a refcounted pointer, call nix_gc_decref.
 *
 * Does not fail
 *
 * @param[in] object The object to keep alive
 */
void nix_gc_incref(const void *);
/**
 * @brief Decrease the GC refcount
 *
 * @param[in] object The object to stop referencing
 */
void nix_gc_decref(const void *);

/**
 * @brief Register a callback that gets called when the object is garbage
 * collected.
 * @note objects can only have a single finalizer. This function overwrites
 * silently.
 * @param[in] obj the object to watch
 * @param[in] cd the data to pass to the finalizer
 * @param[in] finalizer the callback function, called with obj and cd
 */
void nix_gc_register_finalizer(void *obj, void *cd,
                               void (*finalizer)(void *obj, void *cd));

// cffi end
#ifdef __cplusplus
}
#endif

#endif // NIX_API_EXPR_H
