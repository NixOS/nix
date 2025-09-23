#include "nix_api_store.h"
#include "nix_api_store_internal.h"
#include "nix_api_util.h"
#include "nix_api_util_internal.h"

#include "nix/main/plugin.hh"
#include "nix/main/loggers.hh"

extern "C" {

nix_err nix_init_plugins(nix_c_context * context)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        nix::initPlugins();
    }
    NIXC_CATCH_ERRS
}

nix_err nix_set_log_format(nix_c_context * context, const char * format)
{
    if (context)
        context->last_err_code = NIX_OK;
    if (format == nullptr)
        return nix_set_err_msg(context, NIX_ERR_UNKNOWN, "Log format is null");
    try {
        nix::setLogFormat(format);
    }
    NIXC_CATCH_ERRS
}

} // extern "C"
