#include "nix/expr/eval.hh"
#include "nix/expr/get-drvs.hh"

#include "nix_api_expr.h"
#include "nix_api_expr_internal.h"
#include "nix_api_store.h"
#include "nix_api_store_internal.h"
#include "nix_api_util.h"
#include "nix_api_util_internal.h"

static const nix::Bindings * get_bindings_or_null(nix_value * autoArgs)
{
    if (!autoArgs) {
        return nullptr;
    }
    auto & v = check_value_in(autoArgs);
    if (v.type() == nix::nAttrs) {
        return v.attrs();
    }
    return nullptr;
}

extern "C" {

StorePath *
nix_get_derivation(nix_c_context * context, EvalState * state, nix_value * value, bool ignoreAssertionFailures)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_in(value);
        auto maybePkg = nix::getDerivation(state->state, v, ignoreAssertionFailures);
        if (!maybePkg) {
            return nullptr;
        }
        nix::StorePath sp = maybePkg->requireDrvPath();
        return new StorePath{std::move(sp)};
    }
    NIXC_CATCH_ERRS_NULL
}

nix_err nix_value_auto_call_function(
    nix_c_context * context, EvalState * state, nix_value * auto_args, nix_value * fn_val, nix_value * result)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & fn = check_value_in(fn_val);
        auto & res = *result->value;

        const nix::Bindings * b = get_bindings_or_null(auto_args);
        if (b) {
            state->state.autoCallFunction(*b, fn, res);
        } else {
            state->state.autoCallFunction(nix::Bindings::emptyBindings, fn, res);
        }
    }
    NIXC_CATCH_ERRS
}

} // extern "C"
