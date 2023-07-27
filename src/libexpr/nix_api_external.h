#ifndef NIX_API_EXTERNAL_H
#define NIX_API_EXTERNAL_H
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
 * @brief Represents a string meant for consumption by nix.
 */
typedef struct nix_returned_string nix_returned_string;
/**
 * @brief Wraps a stream that can output multiple string pieces.
 */
typedef struct nix_printer nix_printer;
/**
 * @brief A list of string context items
 */
typedef struct nix_string_context nix_string_context;

/**
 * @brief Allocate a nix_returned_string from a const char*.
 *
 * Copies the passed string.
 * @param[in] c The string to copy
 * @returns A nix_returned_string*
 */
nix_returned_string *nix_external_alloc_string(const char *c);

/**
 * @brief Deallocate a nix_returned_string
 *
 * There's generally no need to call this, since
 * returning the string will pass ownership to nix,
 * but you can use it in case of errors.
 * @param[in] str The string to deallocate
 */
void nix_external_dealloc_string(nix_returned_string *str);

/**
 * Print to the nix_printer
 *
 * @param[out] context Optional, stores error information
 * @param printer The nix_printer to print to
 * @param[in] str The string to print
 * @returns NIX_OK if everything worked
 */
nix_err nix_external_print(nix_c_context *context, nix_printer *printer,
                           const char *str);

/**
 * Add string context to the nix_string_context object
 * @param[out] context Optional, stores error information
 * @param[out] string_context The nix_string_context to add to
 * @param[in] c The context string to add
 * @returns NIX_OK if everything worked
 */
nix_err nix_external_add_string_context(nix_c_context *context,
                                        nix_string_context *string_context,
                                        const char *c);

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
typedef struct NixCExternalValueDesc {
  /**
   * @brief Called when printing the external value
   *
   * @param[in] self the void* passed to nix_create_external_value
   * @param[out] printer The printer to print to, pass to nix_external_print
   */
  void (*print)(void *self, nix_printer *printer);
  /**
   * @brief Called on :t
   * @param[in] self the void* passed to nix_create_external_value
   * @returns a nix_returned_string, ownership passed to nix
   */
  nix_returned_string *(*showType)(void *self); // std::string
  /**
   * @brief Called on `builtins.typeOf`
   * @param self the void* passed to nix_create_external_value
   * @returns a nix_returned_string, ownership passed to nix
   */
  nix_returned_string *(*typeOf)(void *self); // std::string
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
   * @returns a nix_returned_string, ownership passed to nix. Optional,
   * returning NULL will make the conversion throw an error.
   */
  nix_returned_string *(*coerceToString)(void *self, nix_string_context *c,
                                         int coerceMore, int copyToStore);
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
  int (*equal)(void *self, void *other);
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
   * @returns string that gets parsed as json. Optional, returning NULL will
   * make the conversion throw an error.
   */
  nix_returned_string *(*printValueAsJSON)(void *self, State *, int strict,
                                           nix_string_context *c,
                                           bool copyToStore);
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
  void (*printValueAsXML)(void *self, State *, int strict, int location,
                          void *doc, nix_string_context *c, void *drvsSeen,
                          int pos);
} NixCExternalValueDesc;

/**
 * @brief Create an external value, that can be given to nix_set_external
 *
 * Pass a gcref to keep a reference.
 * @param[out] context Optional, stores error information
 * @param[in] desc a NixCExternalValueDesc, you should keep this alive as long
 * as the ExternalValue lives
 * @param[in] v the value to store
 * @param[out] ref Optional, will store a reference to the returned value.
 * @returns external value, owned by the garbage collector
 * @see nix_set_external
 */
ExternalValue *nix_create_external_value(nix_c_context *context,
                                         NixCExternalValueDesc *desc, void *v,
                                         GCRef *ref);

/**
 * @brief Extract the pointer from a nix c external value.
 * @param[out] context Optional, stores error information
 * @param[in] b The external value
 * @returns The pointer, or null if the external value was not from nix c.
 * @see nix_get_external
 */
void *nix_get_external_value_content(nix_c_context *context, ExternalValue *b);

// cffi end
#ifdef __cplusplus
}
#endif

#endif // NIX_API_EXTERNAL_H
