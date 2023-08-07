#ifndef NIX_API_UTIL_INTERNAL_H
#define NIX_API_UTIL_INTERNAL_H

#include <string>

// forward declaration
namespace nix {
class Error;
};

struct nix_c_context {
  nix_err last_err_code = NIX_OK;
  std::optional<std::string> last_err = {};
  std::optional<nix::ErrorInfo> info = {};
  std::string name = "";
};

nix_err nix_context_error(nix_c_context *context);

/**
 * Internal use only.
 *
 * Sets the most recent error message.
 *
 * @param context context to write the error message to, or NULL
 * @param err The error code to set and return
 * @param msg The error message to set.
 * @returns the error code set
 */
nix_err nix_set_err_msg(nix_c_context *context, nix_err err, const char *msg);

/**
 * Internal use only.
 *
 * Export a std::string across the C api boundary
 * @param context optional, the context to store errors in if this function
 * fails
 * @param str The string to export
 * @param value The allocated area to write the string to.
 * @param n Maximum size of the returned string.
 * @return NIX_OK if there were no errors, NIX_ERR_OVERFLOW if the string length
 * exceeds `n`.
 */
nix_err nix_export_std_string(nix_c_context *context,
                              const std::string_view str, char *dest,
                              unsigned int n);

#define NIXC_CATCH_ERRS                                                        \
  catch (...) {                                                                \
    return nix_context_error(context);                                         \
  }                                                                            \
  return NIX_OK;

#define NIXC_CATCH_ERRS_RES(def)                                               \
  catch (...) {                                                                \
    nix_context_error(context);                                                \
    return def;                                                                \
  }
#define NIXC_CATCH_ERRS_NULL NIXC_CATCH_ERRS_RES(nullptr)

#endif // NIX_API_UTIL_INTERNAL_H
