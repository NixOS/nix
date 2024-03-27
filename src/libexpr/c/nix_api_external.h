#ifndef NIX_API_EXTERNAL_H
#define NIX_API_EXTERNAL_H
/** @ingroup libexpr
 * @addtogroup Externals
 * @brief Deal with external values
 * @{
 */
/** @file
 * @brief libexpr C bindings dealing with external values
 */

#include "nix_api_expr.h"
#include "nix_api_util.h"
#include "nix_api_value.h"
#include "stdbool.h"
#include "stddef.h"
#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif
// cffi start

/**
 * @brief Represents a string owned by the Nix language evaluator.
 * @see nix_set_owned_string
 */
typedef struct nix_string_return nix_string_return;
/**
 * @brief Wraps a stream that can output multiple string pieces.
 */
typedef struct nix_printer nix_printer;
/**
 * @brief A list of string context items
 */
typedef struct nix_string_context nix_string_context;

/**
 * @brief Sets the contents of a nix_string_return
 *
 * Copies the passed string.
 * @param[out] str the nix_string_return to write to
 * @param[in]  c   The string to copy
 */
void nix_set_string_return(nix_string_return * str, const char * c);

/**
 * Print to the nix_printer
 *
 * @param[out] context Optional, stores error information
 * @param printer The nix_printer to print to
 * @param[in] str The string to print
 * @returns NIX_OK if everything worked
 */
nix_err nix_external_print(nix_c_context * context, nix_printer * printer, const char * str);

/**
 * Add string context to the nix_string_context object
 * @param[out] context Optional, stores error information
 * @param[out] string_context The nix_string_context to add to
 * @param[in] c The context string to add
 * @returns NIX_OK if everything worked
 */
nix_err nix_external_add_string_context(nix_c_context * context, nix_string_context * string_context, const char * c);

/**
 * @brief Definition for a class of external values
 *
 * Create and implement one of these, then pass it to nix_create_external_value
 * Make sure to keep it alive while the external value lives.
 *
 * Optional functions can be set to NULL
 *
 * @see nix_create_external_value
 */
typedef struct NixCExternalValueDesc
{
    /**
     * @brief Called when printing the external value
     *
     * @param[in] self the void* passed to nix_create_external_value
     * @param[out] printer The printer to print to, pass to nix_external_print
     */
    void (*print)(void * self, nix_printer * printer);
    /**
     * @brief Called on :t
     * @param[in] self the void* passed to nix_create_external_value
     * @param[out] res the return value
     */
    void (*showType)(void * self, nix_string_return * res);
    /**
     * @brief Called on `builtins.typeOf`
     * @param self the void* passed to nix_create_external_value
     * @param[out] res the return value
     */
    void (*typeOf)(void * self, nix_string_return * res);
    /**
     * @brief Called on "${str}" and builtins.toString.
     *
     * The latter with coerceMore=true
     * Optional, the default is to throw an error.
     * @param[in] self the void* passed to nix_create_external_value
     * @param[out] c writable string context for the resulting string
     * @param[in] coerceMore boolean, try to coerce to strings in more cases
     * instead of throwing an error
     * @param[in] copyToStore boolean, whether to copy referenced paths to store
     * or keep them as-is
     * @param[out] res the return value. Not touching this, or setting it to the
     * empty string, will make the conversion throw an error.
     */
    void (*coerceToString)(
        void * self, nix_string_context * c, int coerceMore, int copyToStore, nix_string_return * res);
    /**
     * @brief Try to compare two external values
     *
     * Optional, the default is always false.
     * If the other object was not a Nix C external value, this comparison will
     * also return false
     * @param[in] self the void* passed to nix_create_external_value
     * @param[in] other the void* passed to the other object's
     * nix_create_external_value
     * @returns true if the objects are deemed to be equal
     */
    int (*equal)(void * self, void * other);
    /**
     * @brief Convert the external value to json
     *
     * Optional, the default is to throw an error
     * @param[in] self the void* passed to nix_create_external_value
     * @param[in] state The evaluator state
     * @param[in] strict boolean Whether to force the value before printing
     * @param[out] c writable string context for the resulting string
     * @param[in] copyToStore whether to copy referenced paths to store or keep
     * them as-is
     * @param[out] res the return value. Gets parsed as JSON. Not touching this,
     * or setting it to the empty string, will make the conversion throw an error.
     */
    void (*printValueAsJSON)(
        void * self, EvalState *, bool strict, nix_string_context * c, bool copyToStore, nix_string_return * res);
    /**
     * @brief Convert the external value to XML
     *
     * Optional, the default is to throw an error
     * @todo The mechanisms for this call are incomplete. There are no C
     *       bindings to work with XML, pathsets and positions.
     * @param[in] self the void* passed to nix_create_external_value
     * @param[in] state The evaluator state
     * @param[in] strict boolean Whether to force the value before printing
     * @param[in] location boolean Whether to include position information in the
     * xml
     * @param[out] doc XML document to output to
     * @param[out] c writable string context for the resulting string
     * @param[in,out] drvsSeen a path set to avoid duplicating derivations
     * @param[in] pos The position of the call.
     */
    void (*printValueAsXML)(
        void * self,
        EvalState *,
        int strict,
        int location,
        void * doc,
        nix_string_context * c,
        void * drvsSeen,
        int pos);
} NixCExternalValueDesc;

/**
 * @brief Create an external value, that can be given to nix_init_external
 *
 * Owned by the GC. Use nix_gc_decref when you're done with the pointer.
 *
 * @param[out] context Optional, stores error information
 * @param[in] desc a NixCExternalValueDesc, you should keep this alive as long
 * as the ExternalValue lives
 * @param[in] v the value to store
 * @returns external value, owned by the garbage collector
 * @see nix_init_external
 */
ExternalValue * nix_create_external_value(nix_c_context * context, NixCExternalValueDesc * desc, void * v);

/**
 * @brief Extract the pointer from a nix c external value.
 * @param[out] context Optional, stores error information
 * @param[in] b The external value
 * @returns The pointer, or null if the external value was not from nix c.
 * @see nix_get_external
 */
void * nix_get_external_value_content(nix_c_context * context, ExternalValue * b);

// cffi end
#ifdef __cplusplus
}
#endif
/** @} */

#endif // NIX_API_EXTERNAL_H
