#ifndef NIX_API_UTIL_INTERNAL_H
#define NIX_API_UTIL_INTERNAL_H

#include <string>
#include <optional>

#include "error.hh"
#include "nix_api_util.h"

struct nix_c_context
{
    nix_err last_err_code = NIX_OK;
    std::optional<std::string> last_err = {};
    std::optional<nix::ErrorInfo> info = {};
    std::string name = "";
};

nix_err nix_context_error(nix_c_context * context);

/**
 * Internal use only.
 *
 * Helper to invoke nix_get_string_callback
 * @param context optional, the context to store errors in if this function
 * fails
 * @param str The string to observe
 * @param callback Called with the observed string.
 * @param user_data optional, arbitrary data, passed to the callback when it's called.
 * @return NIX_OK if there were no errors.
 * @see nix_get_string_callback
 */
nix_err call_nix_get_string_callback(const std::string str, void * callback, void * user_data);

#define NIXC_CATCH_ERRS \
    catch (...) \
    { \
        return nix_context_error(context); \
    } \
    return NIX_OK;

#define NIXC_CATCH_ERRS_RES(def) \
    catch (...) \
    { \
        nix_context_error(context); \
        return def; \
    }
#define NIXC_CATCH_ERRS_NULL NIXC_CATCH_ERRS_RES(nullptr)

#endif // NIX_API_UTIL_INTERNAL_H
