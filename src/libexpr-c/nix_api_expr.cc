#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#include "config.hh"
#include "eval.hh"
#include "globals.hh"
#include "util.hh"
#include "eval-settings.hh"

#include "nix_api_expr.h"
#include "nix_api_expr_internal.h"
#include "nix_api_store.h"
#include "nix_api_store_internal.h"
#include "nix_api_util.h"
#include "nix_api_util_internal.h"

#ifdef HAVE_BOEHMGC
#include <mutex>
#define GC_INCLUDE_NEW 1
#include "gc_cpp.h"
#endif

nix_err nix_libexpr_init(nix_c_context * context)
{
    if (context)
        context->last_err_code = NIX_OK;
    {
        auto ret = nix_libutil_init(context);
        if (ret != NIX_OK)
            return ret;
    }
    {
        auto ret = nix_libstore_init(context);
        if (ret != NIX_OK)
            return ret;
    }
    try {
        nix::initGC();
    }
    NIXC_CATCH_ERRS
}

nix_err nix_expr_eval_from_string(
    nix_c_context * context, EvalState * state, const char * expr, const char * path, nix_value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        nix::Expr * parsedExpr = state->state.parseExprFromString(expr, state->state.rootPath(nix::CanonPath(path)));
        state->state.eval(parsedExpr, value->value);
        state->state.forceValue(value->value, nix::noPos);
    }
    NIXC_CATCH_ERRS
}

nix_err nix_value_call(nix_c_context * context, EvalState * state, Value * fn, nix_value * arg, nix_value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        state->state.callFunction(fn->value, arg->value, value->value, nix::noPos);
        state->state.forceValue(value->value, nix::noPos);
    }
    NIXC_CATCH_ERRS
}

nix_err nix_value_call_multi(nix_c_context * context, EvalState * state, nix_value * fn, size_t nargs, nix_value ** args, nix_value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        state->state.callFunction(fn->value, nargs, (nix::Value * *)args, value->value, nix::noPos);
        state->state.forceValue(value->value, nix::noPos);
    }
    NIXC_CATCH_ERRS
}

nix_err nix_value_force(nix_c_context * context, EvalState * state, nix_value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        state->state.forceValue(value->value, nix::noPos);
    }
    NIXC_CATCH_ERRS
}

nix_err nix_value_force_deep(nix_c_context * context, EvalState * state, nix_value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        state->state.forceValueDeep(value->value);
    }
    NIXC_CATCH_ERRS
}

EvalState * nix_state_create(nix_c_context * context, const char ** lookupPath_c, Store * store)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        nix::Strings lookupPath;
        if (lookupPath_c != nullptr)
            for (size_t i = 0; lookupPath_c[i] != nullptr; i++)
                lookupPath.push_back(lookupPath_c[i]);

        void * p = ::operator new(
            sizeof(EvalState),
            static_cast<std::align_val_t>(alignof(EvalState)));
        auto * p2 = static_cast<EvalState *>(p);
        new (p) EvalState {
            .settings = nix::EvalSettings{
                nix::settings.readOnlyMode,
            },
            .state = nix::EvalState(
                nix::LookupPath::parse(lookupPath),
                store->ptr,
                p2->settings),
        };
        loadConfFile(p2->settings);
        return p2;
    }
    NIXC_CATCH_ERRS_NULL
}

void nix_state_free(EvalState * state)
{
    delete state;
}

#ifdef HAVE_BOEHMGC
std::unordered_map<
    const void *,
    unsigned int,
    std::hash<const void *>,
    std::equal_to<const void *>,
    traceable_allocator<std::pair<const void * const, unsigned int>>>
    nix_refcounts;

std::mutex nix_refcount_lock;

nix_err nix_gc_incref(nix_c_context * context, const void * p)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        std::scoped_lock lock(nix_refcount_lock);
        auto f = nix_refcounts.find(p);
        if (f != nix_refcounts.end()) {
            f->second++;
        } else {
            nix_refcounts[p] = 1;
        }
    }
    NIXC_CATCH_ERRS
}

nix_err nix_gc_decref(nix_c_context * context, const void * p)
{

    if (context)
        context->last_err_code = NIX_OK;
    try {
        std::scoped_lock lock(nix_refcount_lock);
        auto f = nix_refcounts.find(p);
        if (f != nix_refcounts.end()) {
            if (--f->second == 0)
                nix_refcounts.erase(f);
        } else
            throw std::runtime_error("nix_gc_decref: object was not referenced");
    }
    NIXC_CATCH_ERRS
}

void nix_gc_now()
{
    GC_gcollect();
}

#else
nix_err nix_gc_incref(nix_c_context * context, const void *)
{
    if (context)
        context->last_err_code = NIX_OK;
    return NIX_OK;
}
nix_err nix_gc_decref(nix_c_context * context, const void *)
{
    if (context)
        context->last_err_code = NIX_OK;
    return NIX_OK;
}
void nix_gc_now() {}
#endif

nix_err nix_value_incref(nix_c_context * context, nix_value *x)
{
    return nix_gc_incref(context, (const void *) x);
}
nix_err nix_value_decref(nix_c_context * context, nix_value *x)
{
    return nix_gc_decref(context, (const void *) x);
}

void nix_gc_register_finalizer(void * obj, void * cd, void (*finalizer)(void * obj, void * cd))
{
#ifdef HAVE_BOEHMGC
    GC_REGISTER_FINALIZER(obj, finalizer, cd, 0, 0);
#endif
}
