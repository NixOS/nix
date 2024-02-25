#ifndef NIX_API_EXPR_H
#define NIX_API_EXPR_H
/** @defgroup libexpr libexpr
 * @brief Bindings to the Nix language evaluator
 *
 * Example (without error handling):
 * @code{.c}
 * int main() {
 *    nix_libexpr_init(NULL);
 *
 *    Store* store = nix_store_open(NULL, "dummy", NULL);
 *    EvalState* state = nix_state_create(NULL, NULL, store); // empty nix path
 *    Value *value = nix_alloc_value(NULL, state);
 *
 *    nix_expr_eval_from_string(NULL, state, "builtins.nixVersion", ".", value);
 *    nix_value_force(NULL, state, value);
 *    printf("nix version: %s\n", nix_get_string(NULL, value));
 *
 *    nix_gc_decref(NULL, value);
 *    nix_state_free(state);
 *    nix_store_free(store);
 *    return 0;
 *    }
 * @endcode
 * @{
 */
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
 * @brief Represents a state of the Nix language evaluator.
 *
 * Multiple states can be created for multi-threaded
 * operation.
 * @struct EvalState
 * @see nix_state_create
 */
typedef struct EvalState EvalState; // nix::EvalState
/**
 * @brief Represents a value in the Nix language.
 *
 * Owned by the garbage collector.
 * @struct Value
 * @see value_manip
 */
typedef void Value; // nix::Value

// Function prototypes
/**
 * @brief Initialize the Nix language evaluator.
 *
 * This function must be called at least once,
 * at some point before constructing a EvalState for the first time.
 * This function can be called multiple times, and is idempotent.
 *
 * @param[out] context Optional, stores error information
 * @return NIX_OK if the initialization was successful, an error code otherwise.
 */
nix_err nix_libexpr_init(nix_c_context * context);

/**
 * @brief Parses and evaluates a Nix expression from a string.
 *
 * @param[out] context Optional, stores error information
 * @param[in] state The state of the evaluation.
 * @param[in] expr The Nix expression to parse.
 * @param[in] path The file path to associate with the expression.
 * This is required for expressions that contain relative paths (such as `./.`) that are resolved relative to the given
 * directory.
 * @param[out] value The result of the evaluation. You must allocate this
 * yourself.
 * @return NIX_OK if the evaluation was successful, an error code otherwise.
 */
nix_err nix_expr_eval_from_string(
    nix_c_context * context, EvalState * state, const char * expr, const char * path, Value * value);

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
nix_err nix_value_call(nix_c_context * context, EvalState * state, Value * fn, Value * arg, Value * value);

/**
 * @brief Forces the evaluation of a Nix value.
 *
 * The Nix interpreter is lazy, and not-yet-evaluated Values can be
 * of type NIX_TYPE_THUNK instead of their actual value.
 *
 * This function converts these Values into their final type.
 *
 * @note You don't need this function for basic API usage, since all functions
 * that return a value call it for you. The only place you will see a
 * NIX_TYPE_THUNK is in the arguments that are passed to a PrimOp function
 * you supplied to nix_alloc_primop.
 *
 * @param[out] context Optional, stores error information
 * @param[in] state The state of the evaluation.
 * @param[in,out] value The Nix value to force.
 * @post value is not of type NIX_TYPE_THUNK
 * @return NIX_OK if the force operation was successful, an error code
 * otherwise.
 */
nix_err nix_value_force(nix_c_context * context, EvalState * state, Value * value);

/**
 * @brief Forces the deep evaluation of a Nix value.
 *
 * Recursively calls nix_value_force
 *
 * @see nix_value_force
 * @warning Calling this function on a recursive data structure will cause a
 * stack overflow.
 * @param[out] context Optional, stores error information
 * @param[in] state The state of the evaluation.
 * @param[in,out] value The Nix value to force.
 * @return NIX_OK if the deep force operation was successful, an error code
 * otherwise.
 */
nix_err nix_value_force_deep(nix_c_context * context, EvalState * state, Value * value);

/**
 * @brief Create a new Nix language evaluator state.
 *
 * @param[out] context Optional, stores error information
 * @param[in] searchPath Array of strings corresponding to entries in NIX_PATH.
 * @param[in] store The Nix store to use.
 * @return A new Nix state or NULL on failure.
 */
EvalState * nix_state_create(nix_c_context * context, const char ** searchPath, Store * store);

/**
 * @brief Frees a Nix state.
 *
 * Does not fail.
 *
 * @param[in] state The state to free.
 */
void nix_state_free(EvalState * state);

/** @addtogroup GC
 * @brief Reference counting and garbage collector operations
 *
 * The Nix language evaluator uses a garbage collector. To ease C interop, we implement
 * a reference counting scheme, where objects will be deallocated
 * when there are no references from the Nix side, and the reference count kept
 * by the C API reaches `0`.
 *
 * Functions returning a garbage-collected object will automatically increase
 * the refcount for you. You should make sure to call `nix_gc_decref` when
 * you're done with a value returned by the evaluator.
 * @{
 */
/**
 * @brief Increment the garbage collector reference counter for the given object.
 *
 * The Nix language evaluator C API keeps track of alive objects by reference counting.
 * When you're done with a refcounted pointer, call nix_gc_decref().
 *
 * @param[out] context Optional, stores error information
 * @param[in] object The object to keep alive
 */
nix_err nix_gc_incref(nix_c_context * context, const void * object);
/**
 * @brief Decrement the garbage collector reference counter for the given object
 *
 * @param[out] context Optional, stores error information
 * @param[in] object The object to stop referencing
 */
nix_err nix_gc_decref(nix_c_context * context, const void * object);

/**
 * @brief Trigger the garbage collector manually
 *
 * You should not need to do this, but it can be useful for debugging.
 */
void nix_gc_now();

/**
 * @brief Register a callback that gets called when the object is garbage
 * collected.
 * @note Objects can only have a single finalizer. This function overwrites existing values
 * silently.
 * @param[in] obj the object to watch
 * @param[in] cd the data to pass to the finalizer
 * @param[in] finalizer the callback function, called with obj and cd
 */
void nix_gc_register_finalizer(void * obj, void * cd, void (*finalizer)(void * obj, void * cd));

/** @} */
// cffi end
#ifdef __cplusplus
}
#endif

/** @} */

#endif // NIX_API_EXPR_H
