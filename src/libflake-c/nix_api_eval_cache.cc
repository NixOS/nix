#include <string>

#include "nix_api_eval_cache.h"
#include "nix_api_eval_cache_internal.hh"
#include "nix_api_util.h"
#include "nix_api_util_internal.h"
#include "nix_api_expr_internal.h"
#include "nix_api_flake_internal.hh"

#include "nix/flake/flake.hh"
#include "nix/expr/eval-cache.hh"
#include "nix/store/store-api.hh"

extern "C" {

nix_eval_cache * nix_eval_cache_open(nix_c_context * context, EvalState * state, nix_locked_flake * locked_flake)
{
    nix_clear_err(context);
    try {
        auto cache = nix::flake::openEvalCache(state->state, locked_flake->lockedFlake);
        return new nix_eval_cache{cache};
    }
    NIXC_CATCH_ERRS_NULL
}

void nix_eval_cache_free(nix_eval_cache * cache)
{
    delete cache;
}

nix_attr_cursor * nix_eval_cache_get_root(nix_c_context * context, nix_eval_cache * cache)
{
    nix_clear_err(context);
    try {
        return new nix_attr_cursor{cache->cache->getRoot()};
    }
    NIXC_CATCH_ERRS_NULL
}

nix_err nix_eval_cache_checkpoint(nix_c_context * context, nix_eval_cache * cache)
{
    nix_clear_err(context);
    try {
        cache->cache->checkpoint();
    }
    NIXC_CATCH_ERRS
}

nix_attr_cursor * nix_attr_cursor_maybe_get_attr(nix_c_context * context, nix_attr_cursor * cursor, const char * name)
{
    nix_clear_err(context);
    try {
        auto child = cursor->cursor->maybeGetAttr(std::string_view(name));
        if (!child)
            return nullptr;
        return new nix_attr_cursor{nix::ref<nix::eval_cache::AttrCursor>(child)};
    }
    NIXC_CATCH_ERRS_NULL
}

void nix_attr_cursor_free(nix_attr_cursor * cursor)
{
    delete cursor;
}

nix_err nix_attr_cursor_get_attrs(
    nix_c_context * context,
    EvalState * state,
    nix_attr_cursor * cursor,
    void (*callback)(const char * name, void * user_data),
    void * user_data)
{
    nix_clear_err(context);
    try {
        auto attrs = cursor->cursor->getAttrs();
        for (auto & sym : attrs) {
            std::string name(std::string_view(state->state.symbols[sym]));
            callback(name.c_str(), user_data);
        }
    }
    NIXC_CATCH_ERRS
}

nix_err nix_attr_cursor_is_derivation(nix_c_context * context, nix_attr_cursor * cursor, bool * out)
{
    nix_clear_err(context);
    try {
        *out = cursor->cursor->isDerivation();
    }
    NIXC_CATCH_ERRS
}

nix_err nix_attr_cursor_get_drv_path(
    nix_c_context * context,
    EvalState * state,
    nix_attr_cursor * cursor,
    nix_get_string_callback callback,
    void * user_data)
{
    nix_clear_err(context);
    try {
        auto drvPath = cursor->cursor->forceDerivation();
        return call_nix_get_string_callback(state->state.store->printStorePath(drvPath), callback, user_data);
    }
    NIXC_CATCH_ERRS
}

nix_err nix_attr_cursor_get_string(
    nix_c_context * context, nix_attr_cursor * cursor, nix_get_string_callback callback, void * user_data)
{
    nix_clear_err(context);
    try {
        return call_nix_get_string_callback(cursor->cursor->getString(), callback, user_data);
    }
    NIXC_CATCH_ERRS
}

nix_err nix_attr_cursor_get_bool(nix_c_context * context, nix_attr_cursor * cursor, bool * out)
{
    nix_clear_err(context);
    try {
        *out = cursor->cursor->getBool();
    }
    NIXC_CATCH_ERRS
}

nix_err nix_attr_cursor_get_list_of_strings(
    nix_c_context * context,
    nix_attr_cursor * cursor,
    void (*callback)(const char * value, void * user_data),
    void * user_data)
{
    nix_clear_err(context);
    try {
        auto list = cursor->cursor->getListOfStrings();
        for (auto & s : list)
            callback(s.c_str(), user_data);
    }
    NIXC_CATCH_ERRS
}

nix_value * nix_attr_cursor_force_value(nix_c_context * context, EvalState * state, nix_attr_cursor * cursor)
{
    nix_clear_err(context);
    try {
        auto & v = cursor->cursor->forceValue();
        nix_value * ret = new (state->state.mem.allocBytes(sizeof(nix_value))) nix_value{
            .value = &v,
            .mem = &state->state.mem,
        };
        nix_gc_incref(nullptr, ret);
        return ret;
    }
    NIXC_CATCH_ERRS_NULL
}

} // extern "C"
