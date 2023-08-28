#ifndef NIX_API_VALUE_H
#define NIX_API_VALUE_H

/** @addtogroup libexpr
 * @{
 */
/** @file
 * @brief libexpr C bindings dealing with values
 */

#include "nix_api_util.h"
#include "stdbool.h"
#include "stddef.h"
#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif
// cffi start

// Type definitions
typedef enum {
    NIX_TYPE_THUNK,
    NIX_TYPE_INT,
    NIX_TYPE_FLOAT,
    NIX_TYPE_BOOL,
    NIX_TYPE_STRING,
    NIX_TYPE_PATH,
    NIX_TYPE_NULL,
    NIX_TYPE_ATTRS,
    NIX_TYPE_LIST,
    NIX_TYPE_FUNCTION,
    NIX_TYPE_EXTERNAL
} ValueType;

// forward declarations
typedef void Value;
typedef struct State State;
// type defs
/** @brief Stores an under-construction set of bindings
 * @ingroup value_manip
 *
 * Do not reuse.
 * @see nix_make_bindings_builder, nix_bindings_builder_free, nix_make_attrs
 * @see nix_bindings_builder_insert
 */
typedef struct BindingsBuilder BindingsBuilder;

/** @brief PrimOp function
 * @ingroup primops
 *
 * Owned by the GC
 * @see nix_alloc_primop, nix_set_primop
 */
typedef struct PrimOp PrimOp;
/** @brief External Value
 * @ingroup Externals
 *
 * Owned by the GC
 */
typedef struct ExternalValue ExternalValue;

/** @defgroup primops
 * @brief Create your own primops
 * @{
 */
/** @brief Function pointer for primops
 * When you want to return an error, call nix_set_err_msg(context, NIX_ERR_UNKNOWN, "your error message here").
 *
 * @param[in] user_data Arbitrary data that was initially supplied to nix_alloc_primop
 * @param[out] context Stores error information.
 * @param[in] state Evaluator state
 * @param[in] args list of arguments. Note that these can be thunks and should be forced using nix_value_force before
 * use.
 * @param[out] ret return value
 * @see nix_alloc_primop, nix_set_primop
 */
typedef void (*PrimOpFun)(void * user_data, nix_c_context * context, State * state, Value ** args, Value * ret);

/** @brief Allocate a PrimOp
 *
 * Owned by the garbage collector.
 * Use nix_gc_decref() when you're done with the returned PrimOp.
 *
 * @param[out] context Optional, stores error information
 * @param[in] fun callback
 * @param[in] arity expected number of function arguments
 * @param[in] name function name
 * @param[in] args array of argument names, NULL-terminated
 * @param[in] doc optional, documentation for this primop
 * @param[in] user_data optional, arbitrary data, passed to the callback when it's called
 * @return primop, or null in case of errors
 * @see nix_set_primop
 */
PrimOp * nix_alloc_primop(
    nix_c_context * context,
    PrimOpFun fun,
    int arity,
    const char * name,
    const char ** args,
    const char * doc,
    void * user_data);

/** @brief add a primop to the `builtins` attribute set
 *
 * Only applies to States created after this call.
 *
 * Moves your PrimOp content into the global evaluator
 * registry, meaning your input PrimOp pointer is no longer usable.
 * You are free to remove your references to it,
 * after which it will be garbage collected.
 *
 * @param[out] context Optional, stores error information
 * @return primop, or null in case of errors
 *
 */
nix_err nix_register_primop(nix_c_context * context, PrimOp * primOp);
/** @} */

// Function prototypes

/** @brief Allocate a Nix value
 *
 * Owned by the GC. Use nix_gc_decref() when you're done with the pointer
 * @param[out] context Optional, stores error information
 * @param[in] state nix evaluator state
 * @return value, or null in case of errors
 *
 */
Value * nix_alloc_value(nix_c_context * context, State * state);
/** @addtogroup value_manip Manipulating values
 * @brief Functions to inspect and change Nix language values, represented by Value.
 * @{
 */
/** @name Getters
 */
/**@{*/
/** @brief Get value type
 * @param[out] context Optional, stores error information
 * @param[in] value Nix value to inspect
 * @return type of nix value
 */
ValueType nix_get_type(nix_c_context * context, const Value * value);
/** @brief Get type name of value as defined in the evaluator
 * @param[out] context Optional, stores error information
 * @param[in] value Nix value to inspect
 * @return type name, owned string
 * @todo way to free the result
 */
const char * nix_get_typename(nix_c_context * context, const Value * value);

/** @brief Get boolean value
 * @param[out] context Optional, stores error information
 * @param[in] value Nix value to inspect
 * @return true or false, error info via context
 */
bool nix_get_bool(nix_c_context * context, const Value * value);
/** @brief Get string
 * @param[out] context Optional, stores error information
 * @param[in] value Nix value to inspect
 * @return string
 * @return NULL in case of error.
 */
const char * nix_get_string(nix_c_context * context, const Value * value);
/** @brief Get path as string
 * @param[out] context Optional, stores error information
 * @param[in] value Nix value to inspect
 * @return string
 * @return NULL in case of error.
 */
const char * nix_get_path_string(nix_c_context * context, const Value * value);
/** @brief Get the length of a list
 * @param[out] context Optional, stores error information
 * @param[in] value Nix value to inspect
 * @return length of list, error info via context
 */
unsigned int nix_get_list_size(nix_c_context * context, const Value * value);
/** @brief Get the element count of an attrset
 * @param[out] context Optional, stores error information
 * @param[in] value Nix value to inspect
 * @return attrset element count, error info via context
 */
unsigned int nix_get_attrs_size(nix_c_context * context, const Value * value);
/** @brief Get float value in 64 bits
 * @param[out] context Optional, stores error information
 * @param[in] value Nix value to inspect
 * @return float contents, error info via context
 */
double nix_get_float(nix_c_context * context, const Value * value);
/** @brief Get int value
 * @param[out] context Optional, stores error information
 * @param[in] value Nix value to inspect
 * @return int contents, error info via context
 */
int64_t nix_get_int(nix_c_context * context, const Value * value);
/** @brief Get external reference
 * @param[out] context Optional, stores error information
 * @param[in] value Nix value to inspect
 * @return reference to external, NULL in case of error
 */
ExternalValue * nix_get_external(nix_c_context * context, Value *);

/** @brief Get the ix'th element of a list
 *
 * Owned by the GC. Use nix_gc_decref when you're done with the pointer
 * @param[out] context Optional, stores error information
 * @param[in] value Nix value to inspect
 * @param[in] state nix evaluator state
 * @param[in] ix list element to get
 * @return value, NULL in case of errors
 */
Value * nix_get_list_byidx(nix_c_context * context, const Value * value, State * state, unsigned int ix);
/** @brief Get an attr by name
 *
 * Owned by the GC. Use nix_gc_decref when you're done with the pointer
 * @param[out] context Optional, stores error information
 * @param[in] value Nix value to inspect
 * @param[in] state nix evaluator state
 * @param[in] name attribute name
 * @return value, NULL in case of errors
 */
Value * nix_get_attr_byname(nix_c_context * context, const Value * value, State * state, const char * name);

/** @brief Check if an attribute name exists on a value
 * @param[out] context Optional, stores error information
 * @param[in] value Nix value to inspect
 * @param[in] state nix evaluator state
 * @param[in] name attribute name
 * @return value, error info via context
 */
bool nix_has_attr_byname(nix_c_context * context, const Value * value, State * state, const char * name);

/** @brief Get an attribute by index in the sorted bindings
 *
 * Also gives you the name.
 *
 * Owned by the GC. Use nix_gc_decref when you're done with the pointer
 * @param[out] context Optional, stores error information
 * @param[in] value Nix value to inspect
 * @param[in] state nix evaluator state
 * @param[in] i attribute index
 * @param[out] name will store a pointer to the attribute name
 * @return value, NULL in case of errors
 */
Value *
nix_get_attr_byidx(nix_c_context * context, const Value * value, State * state, unsigned int i, const char ** name);

/** @brief Get an attribute name by index in the sorted bindings
 *
 * Useful when you want the name but want to avoid evaluation.
 *
 * Owned by the nix State
 * @param[out] context Optional, stores error information
 * @param[in] value Nix value to inspect
 * @param[in] state nix evaluator state
 * @param[in] i attribute index
 * @return name, NULL in case of errors
 */
const char * nix_get_attr_name_byidx(nix_c_context * context, const Value * value, State * state, unsigned int i);
/**@}*/
/** @name Setters
 */
/**@{*/
/** @brief Set boolean value
 * @param[out] context Optional, stores error information
 * @param[out] value Nix value to modify
 * @param[in] b the boolean value
 * @return error code, NIX_OK on success.
 */
nix_err nix_set_bool(nix_c_context * context, Value * value, bool b);
/** @brief Set a string
 * @param[out] context Optional, stores error information
 * @param[out] value Nix value to modify
 * @param[in] str the string, copied
 * @return error code, NIX_OK on success.
 */
nix_err nix_set_string(nix_c_context * context, Value * value, const char * str);
/** @brief Set a path
 * @param[out] context Optional, stores error information
 * @param[out] value Nix value to modify
 * @param[in] str the path string, copied
 * @return error code, NIX_OK on success.
 */
nix_err nix_set_path_string(nix_c_context * context, Value * value, const char * str);
/** @brief Set a float
 * @param[out] context Optional, stores error information
 * @param[out] value Nix value to modify
 * @param[in] d the float, 64-bits
 * @return error code, NIX_OK on success.
 */
nix_err nix_set_float(nix_c_context * context, Value * value, double d);
/** @brief Set an int
 * @param[out] context Optional, stores error information
 * @param[out] value Nix value to modify
 * @param[in] i the int
 * @return error code, NIX_OK on success.
 */
nix_err nix_set_int(nix_c_context * context, Value * value, int64_t i);
/** @brief Set null
 * @param[out] context Optional, stores error information
 * @param[out] value Nix value to modify
 * @return error code, NIX_OK on success.
 */
nix_err nix_set_null(nix_c_context * context, Value * value);
/** @brief Set an external value
 * @param[out] context Optional, stores error information
 * @param[out] value Nix value to modify
 * @param[in] val the external value to set. Will be GC-referenced by the value.
 * @return error code, NIX_OK on success.
 */
nix_err nix_set_external(nix_c_context * context, Value * value, ExternalValue * val);
/** @brief Allocate a list
 * @param[out] context Optional, stores error information
 * @param[out] value Nix value to modify
 * @param[in] size size of list
 * @return error code, NIX_OK on success.
 */
nix_err nix_make_list(nix_c_context * context, State * s, Value * value, unsigned int size);
/** @brief Manipulate a list by index
 *
 * Don't do this mid-computation.
 * @pre your list should be at least 'ix+1' items long
 * @param[out] context Optional, stores error information
 * @param[out] value Nix value to modify
 * @param[in] ix index to manipulate
 * @param[in] elem the value to set, will be gc-referenced by the value
 * @return error code, NIX_OK on success.
 */
nix_err nix_set_list_byidx(nix_c_context * context, Value * value, unsigned int ix, Value * elem);
/** @brief Create an attribute set from a bindings builder
 * @param[out] context Optional, stores error information
 * @param[out] value Nix value to modify
 * @param[in] b bindings builder to use. Make sure to unref this afterwards.
 * @return error code, NIX_OK on success.
 */
nix_err nix_make_attrs(nix_c_context * context, Value * value, BindingsBuilder * b);
/** @brief Set primop
 * @param[out] context Optional, stores error information
 * @param[out] value Nix value to modify
 * @param[in] op primop, will be gc-referenced by the value
 * @see nix_alloc_primop
 * @return error code, NIX_OK on success.
 */
nix_err nix_set_primop(nix_c_context * context, Value * value, PrimOp * op);
/** @brief Copy from another value
 * @param[out] context Optional, stores error information
 * @param[out] value Nix value to modify
 * @param[in] source value to copy from
 * @return error code, NIX_OK on success.
 */
nix_err nix_copy_value(nix_c_context * context, Value * value, Value * source);
/**@}*/

/** @brief Create a bindings builder

* @param[out] context Optional, stores error information
* @param[in] state nix evaluator state
* @param[in] capacity how many bindings you'll add. Don't exceed.
* @return owned reference to a bindings builder. Make sure to unref when you're
done.
*/
BindingsBuilder * nix_make_bindings_builder(nix_c_context * context, State * state, size_t capacity);
/** @brief Insert bindings into a builder
 * @param[out] context Optional, stores error information
 * @param[in] builder BindingsBuilder to insert into
 * @param[in] name attribute name, copied into the symbol store
 * @param[in] value value to give the binding
 * @return error code, NIX_OK on success.
 */
nix_err
nix_bindings_builder_insert(nix_c_context * context, BindingsBuilder * builder, const char * name, Value * value);
/** @brief Free a bindings builder
 *
 * Does not fail.
 * @param[in] builder the builder to free
 */
void nix_bindings_builder_free(BindingsBuilder * builder);
/**@}*/

// cffi end
#ifdef __cplusplus
}
#endif

/** @} */
#endif // NIX_API_VALUE_H
