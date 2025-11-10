#include <cstring>
#include <stdexcept>
#include <string>

#include "nix/expr/eval.hh"
#include "nix/expr/eval-gc.hh"
#include "nix/store/globals.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/util/ref.hh"

#include "nix_api_expr.h"
#include "nix_api_expr_internal.h"
#include "nix_api_store.h"
#include "nix_api_store_internal.h"
#include "nix_api_util.h"
#include "nix_api_util_internal.h"

#if NIX_USE_BOEHMGC
#  include <boost/unordered/concurrent_flat_map.hpp>
#endif

/**
 * @brief Allocate and initialize using self-reference
 *
 * This allows a brace initializer to reference the object being constructed.
 *
 * @warning Use with care, as the pointer points to an object that is not fully constructed yet.
 *
 * @tparam T Type to allocate
 * @tparam F A function type for `init`, taking a T* and returning the initializer for T
 * @param init Function that takes a T* and returns the initializer for T
 * @return Pointer to allocated and initialized object
 */
template<typename T, typename F>
static T * unsafe_new_with_self(F && init)
{
    // Allocate
    void * p = ::operator new(sizeof(T), static_cast<std::align_val_t>(alignof(T)));
    // Initialize with placement new
    return new (p) T(init(static_cast<T *>(p)));
}

extern "C" {

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

nix_err nix_value_call_multi(
    nix_c_context * context, EvalState * state, nix_value * fn, size_t nargs, nix_value ** args, nix_value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        state->state.callFunction(fn->value, {(nix::Value **) args, nargs}, value->value, nix::noPos);
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

nix_eval_state_builder * nix_eval_state_builder_new(nix_c_context * context, Store * store)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        return unsafe_new_with_self<nix_eval_state_builder>([&](auto * self) {
            return nix_eval_state_builder{
                .store = nix::ref<nix::Store>(store->ptr),
                .settings = nix::EvalSettings{/* &bool */ self->readOnlyMode},
                .fetchSettings = nix::fetchers::Settings{},
                .readOnlyMode = true,
            };
        });
    }
    NIXC_CATCH_ERRS_NULL
}

void nix_eval_state_builder_free(nix_eval_state_builder * builder)
{
    operator delete(builder, static_cast<std::align_val_t>(alignof(nix_eval_state_builder)));
}

nix_err nix_eval_state_builder_load(nix_c_context * context, nix_eval_state_builder * builder)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        // TODO: load in one go?
        builder->settings.readOnlyMode = nix::settings.readOnlyMode;
        loadConfFile(builder->settings);
        loadConfFile(builder->fetchSettings);
    }
    NIXC_CATCH_ERRS
}

nix_err nix_eval_state_builder_set_lookup_path(
    nix_c_context * context, nix_eval_state_builder * builder, const char ** lookupPath_c)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        nix::Strings lookupPath;
        if (lookupPath_c != nullptr)
            for (size_t i = 0; lookupPath_c[i] != nullptr; i++)
                lookupPath.push_back(lookupPath_c[i]);
        builder->lookupPath = nix::LookupPath::parse(lookupPath);
    }
    NIXC_CATCH_ERRS
}

EvalState * nix_eval_state_build(nix_c_context * context, nix_eval_state_builder * builder)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        return unsafe_new_with_self<EvalState>([&](auto * self) {
            return EvalState{
                .fetchSettings = std::move(builder->fetchSettings),
                .settings = std::move(builder->settings),
                .state = nix::EvalState(builder->lookupPath, builder->store, self->fetchSettings, self->settings),
            };
        });
    }
    NIXC_CATCH_ERRS_NULL
}

EvalState * nix_state_create(nix_c_context * context, const char ** lookupPath_c, Store * store)
{
    auto builder = nix_eval_state_builder_new(context, store);
    if (builder == nullptr)
        return nullptr;

    if (nix_eval_state_builder_load(context, builder) != NIX_OK)
        return nullptr;

    if (nix_eval_state_builder_set_lookup_path(context, builder, lookupPath_c) != NIX_OK)
        return nullptr;

    auto * state = nix_eval_state_build(context, builder);
    nix_eval_state_builder_free(builder);
    return state;
}

void nix_state_free(EvalState * state)
{
    operator delete(state, static_cast<std::align_val_t>(alignof(EvalState)));
}

#if NIX_USE_BOEHMGC
boost::concurrent_flat_map<
    const void *,
    unsigned int,
    std::hash<const void *>,
    std::equal_to<const void *>,
    traceable_allocator<std::pair<const void * const, unsigned int>>>
    nix_refcounts{};

nix_err nix_gc_incref(nix_c_context * context, const void * p)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        nix_refcounts.insert_or_visit({p, 1}, [](auto & kv) { kv.second++; });
    }
    NIXC_CATCH_ERRS
}

nix_err nix_gc_decref(nix_c_context * context, const void * p)
{

    if (context)
        context->last_err_code = NIX_OK;
    try {
        bool fail = true;
        nix_refcounts.erase_if(p, [&](auto & kv) {
            fail = false;
            return !--kv.second;
        });
        if (fail)
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

nix_err nix_value_incref(nix_c_context * context, nix_value * x)
{
    return nix_gc_incref(context, (const void *) x);
}

nix_err nix_value_decref(nix_c_context * context, nix_value * x)
{
    return nix_gc_decref(context, (const void *) x);
}

void nix_gc_register_finalizer(void * obj, void * cd, void (*finalizer)(void * obj, void * cd))
{
#if NIX_USE_BOEHMGC
    GC_REGISTER_FINALIZER(obj, finalizer, cd, 0, 0);
#endif
}

} // extern "C"
