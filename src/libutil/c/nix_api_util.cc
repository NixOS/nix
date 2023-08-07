#include "nix_api_util.h"
#include "config.hh"
#include "error.hh"
#include "nix_api_util_internal.h"
#include "util.hh"

#include <cxxabi.h>
#include <typeinfo>

nix_c_context *nix_c_context_create() { return new nix_c_context(); }

void nix_c_context_free(nix_c_context *context) { delete context; }

nix_err nix_context_error(nix_c_context *context) {
  if (context == nullptr) {
    throw;
  }
  try {
    throw;
  } catch (nix::Error &e) {
    /* Storing this exception is annoying, take what we need here */
    context->last_err = e.what();
    context->info = e.info();
    int status;
    const char *demangled =
        abi::__cxa_demangle(typeid(e).name(), 0, 0, &status);
    if (demangled) {
      context->name = demangled;
      // todo: free(demangled);
    } else {
      context->name = typeid(e).name();
    }
    context->last_err_code = NIX_ERR_NIX_ERROR;
    return context->last_err_code;
  } catch (const std::exception &e) {
    context->last_err = e.what();
    context->last_err_code = NIX_ERR_UNKNOWN;
    return context->last_err_code;
  }
  // unreachable
}

nix_err nix_set_err_msg(nix_c_context *context, nix_err err, const char *msg) {
  if (context == nullptr) {
    // todo last_err_code
    throw nix::Error("Nix C api error: %s", msg);
  }
  context->last_err_code = err;
  context->last_err = msg;
  return err;
}

const char *nix_version_get() { return PACKAGE_VERSION; }

// Implementations
nix_err nix_setting_get(nix_c_context *context, const char *key, char *value,
                        int n) {
  if (context)
    context->last_err_code = NIX_OK;
  try {
    std::map<std::string, nix::AbstractConfig::SettingInfo> settings;
    nix::globalConfig.getSettings(settings);
    if (settings.contains(key))
      return nix_export_std_string(context, settings[key].value, value, n);
    else {
      return nix_set_err_msg(context, NIX_ERR_KEY, "Setting not found");
    }
  }
  NIXC_CATCH_ERRS
}

nix_err nix_setting_set(nix_c_context *context, const char *key,
                        const char *value) {
  if (context)
    context->last_err_code = NIX_OK;
  if (nix::globalConfig.set(key, value))
    return NIX_OK;
  else {
    return nix_set_err_msg(context, NIX_ERR_KEY, "Setting not found");
  }
}

nix_err nix_libutil_init(nix_c_context *context) {
  if (context)
    context->last_err_code = NIX_OK;
  try {
    nix::initLibUtil();
    return NIX_OK;
  }
  NIXC_CATCH_ERRS
}

const char *nix_err_msg(nix_c_context *context,
                        const nix_c_context *read_context, unsigned int *n) {
  if (context)
    context->last_err_code = NIX_OK;
  if (read_context->last_err) {
    if (n)
      *n = read_context->last_err->size();
    return read_context->last_err->c_str();
  }
  nix_set_err_msg(context, NIX_ERR_UNKNOWN, "No error message");
  return nullptr;
}

nix_err nix_err_name(nix_c_context *context, const nix_c_context *read_context,
                     char *value, int n) {
  if (context)
    context->last_err_code = NIX_OK;
  if (read_context->last_err_code != NIX_ERR_NIX_ERROR) {
    return nix_set_err_msg(context, NIX_ERR_UNKNOWN,
                           "Last error was not a nix error");
  }
  return nix_export_std_string(context, read_context->name, value, n);
}

nix_err nix_err_info_msg(nix_c_context *context,
                         const nix_c_context *read_context, char *value,
                         int n) {
  if (context)
    context->last_err_code = NIX_OK;
  if (read_context->last_err_code != NIX_ERR_NIX_ERROR) {
    return nix_set_err_msg(context, NIX_ERR_UNKNOWN,
                           "Last error was not a nix error");
  }
  return nix_export_std_string(context, read_context->info->msg.str(), value,
                               n);
}

nix_err nix_err_code(const nix_c_context *read_context) {
  return read_context->last_err_code;
}

// internal
nix_err nix_export_std_string(nix_c_context *context,
                              const std::string_view str, char *dest,
                              unsigned int n) {
  size_t i = str.copy(dest, n - 1);
  dest[i] = 0;
  if (i == n - 1) {
    return nix_set_err_msg(context, NIX_ERR_OVERFLOW,
                           "Provided buffer too short");
  } else
    return NIX_OK;
}
