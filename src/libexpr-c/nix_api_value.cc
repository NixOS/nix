#include "nix/expr/attr-set.hh"
#include "nix/util/configuration.hh"
#include "nix/expr/eval.hh"
#include "nix/store/globals.hh"
#include "nix/store/path.hh"
#include "nix/expr/primops.hh"
#include "nix/expr/value.hh"

#include "nix_api_expr.h"
#include "nix_api_expr_internal.h"
#include "nix_api_util.h"
#include "nix_api_util_internal.h"
#include "nix_api_store_internal.h"
#include "nix_api_value.h"
#include "nix/expr/value/context.hh"

// Internal helper functions to check [in] and [out] `Value *` parameters
static const nix::Value & check_value_not_null(const nix_value * value)
{
    if (!value) {
        throw std::runtime_error("nix_value is null");
    }
    return *((const nix::Value *) value);
}

static nix::Value & check_value_not_null(nix_value * value)
{
    if (!value) {
        throw std::runtime_error("nix_value is null");
    }
    return value->value;
}

static const nix::Value & check_value_in(const nix_value * value)
{
    auto & v = check_value_not_null(value);
    if (!v.isValid()) {
        throw std::runtime_error("Uninitialized nix_value");
    }
    return v;
}

static nix::Value & check_value_in(nix_value * value)
{
    auto & v = check_value_not_null(value);
    if (!v.isValid()) {
        throw std::runtime_error("Uninitialized nix_value");
    }
    return v;
}

static nix::Value & check_value_out(nix_value * value)
{
    auto & v = check_value_not_null(value);
    if (v.isValid()) {
        throw std::runtime_error("nix_value already initialized. Variables are immutable");
    }
    return v;
}

static inline nix_value * as_nix_value_ptr(nix::Value * v)
{
    return reinterpret_cast<nix_value *>(v);
}

/**
 * Helper function to convert calls from nix into C API.
 *
 * Deals with errors and converts arguments from C++ into C types.
 */
static void nix_c_primop_wrapper(
    PrimOpFun f, void * userdata, nix::EvalState & state, const nix::PosIdx pos, nix::Value ** args, nix::Value & v)
{
    nix_c_context ctx;

    // v currently has a thunk, but the C API initializers require an uninitialized value.
    //
    // We can't destroy the thunk, because that makes it impossible to retry,
    // which is needed for tryEval and for evaluation drivers that evaluate more
    // than one value (e.g. an attrset with two derivations, both of which
    // reference v).
    //
    // Instead we create a temporary value, and then assign the result to v.
    // This does not give the primop definition access to the thunk, but that's
    // ok because we don't see a need for this yet (e.g. inspecting thunks,
    // or maybe something to make blackholes work better; we don't know).
    nix::Value vTmp;

    f(userdata, &ctx, (EvalState *) &state, (nix_value **) args, (nix_value *) &vTmp);

    if (ctx.last_err_code != NIX_OK) {
        /* TODO: Throw different errors depending on the error code */
        state.error<nix::EvalError>("Error from custom function: %s", *ctx.last_err).atPos(pos).debugThrow();
    }

    if (!vTmp.isValid()) {
        state.error<nix::EvalError>("Implementation error in custom function: return value was not initialized")
            .atPos(pos)
            .debugThrow();
    }

    if (vTmp.type() == nix::nThunk) {
        // We might allow this in the future if it makes sense for the evaluator
        // e.g. implementing tail recursion by returning a thunk to the next
        // "iteration". Until then, this is most likely a mistake or misunderstanding.
        state.error<nix::EvalError>("Implementation error in custom function: return value must not be a thunk")
            .atPos(pos)
            .debugThrow();
    }

    v = vTmp;
}

extern "C" {

PrimOp * nix_alloc_primop(
    nix_c_context * context,
    PrimOpFun fun,
    int arity,
    const char * name,
    const char ** args,
    const char * doc,
    void * user_data)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        using namespace std::placeholders;
        auto p = new
#if NIX_USE_BOEHMGC
            (GC)
#endif
                nix::PrimOp{
                    .name = name,
                    .args = {},
                    .arity = (size_t) arity,
                    .doc = doc,
                    .fun = std::bind(nix_c_primop_wrapper, fun, user_data, _1, _2, _3, _4)};
        if (args)
            for (size_t i = 0; args[i]; i++)
                p->args.emplace_back(*args);
        nix_gc_incref(nullptr, p);
        return (PrimOp *) p;
    }
    NIXC_CATCH_ERRS_NULL
}

nix_err nix_register_primop(nix_c_context * context, PrimOp * primOp)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        nix::RegisterPrimOp r(std::move(*((nix::PrimOp *) primOp)));
    }
    NIXC_CATCH_ERRS
}

nix_value * nix_alloc_value(nix_c_context * context, EvalState * state)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        nix_value * res = as_nix_value_ptr(state->state.allocValue());
        nix_gc_incref(nullptr, res);
        return res;
    }
    NIXC_CATCH_ERRS_NULL
}

ValueType nix_get_type(nix_c_context * context, const nix_value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_in(value);
        using namespace nix;
        switch (v.type()) {
        case nThunk:
            return NIX_TYPE_THUNK;
        case nInt:
            return NIX_TYPE_INT;
        case nFloat:
            return NIX_TYPE_FLOAT;
        case nBool:
            return NIX_TYPE_BOOL;
        case nString:
            return NIX_TYPE_STRING;
        case nPath:
            return NIX_TYPE_PATH;
        case nNull:
            return NIX_TYPE_NULL;
        case nAttrs:
            return NIX_TYPE_ATTRS;
        case nList:
            return NIX_TYPE_LIST;
        case nFunction:
            return NIX_TYPE_FUNCTION;
        case nExternal:
            return NIX_TYPE_EXTERNAL;
        }
        return NIX_TYPE_NULL;
    }
    NIXC_CATCH_ERRS_RES(NIX_TYPE_NULL);
}

const char * nix_get_typename(nix_c_context * context, const nix_value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_in(value);
        auto s = nix::showType(v);
        return strdup(s.c_str());
    }
    NIXC_CATCH_ERRS_NULL
}

bool nix_get_bool(nix_c_context * context, const nix_value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_in(value);
        assert(v.type() == nix::nBool);
        return v.boolean();
    }
    NIXC_CATCH_ERRS_RES(false);
}

nix_err
nix_get_string(nix_c_context * context, const nix_value * value, nix_get_string_callback callback, void * user_data)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_in(value);
        assert(v.type() == nix::nString);
        call_nix_get_string_callback(v.string_view(), callback, user_data);
    }
    NIXC_CATCH_ERRS
}

const char * nix_get_path_string(nix_c_context * context, const nix_value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_in(value);
        assert(v.type() == nix::nPath);
        // NOTE (from @yorickvP)
        // v._path.path should work but may not be how Eelco intended it.
        // Long-term this function should be rewritten to copy some data into a
        // user-allocated string.
        // We could use v.path().to_string().c_str(), but I'm concerned this
        // crashes. Looks like .path() allocates a CanonPath with a copy of the
        // string, then it gets the underlying data from that.
        return v.pathStr();
    }
    NIXC_CATCH_ERRS_NULL
}

unsigned int nix_get_list_size(nix_c_context * context, const nix_value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_in(value);
        assert(v.type() == nix::nList);
        return v.listSize();
    }
    NIXC_CATCH_ERRS_RES(0);
}

unsigned int nix_get_attrs_size(nix_c_context * context, const nix_value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_in(value);
        assert(v.type() == nix::nAttrs);
        return v.attrs()->size();
    }
    NIXC_CATCH_ERRS_RES(0);
}

double nix_get_float(nix_c_context * context, const nix_value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_in(value);
        assert(v.type() == nix::nFloat);
        return v.fpoint();
    }
    NIXC_CATCH_ERRS_RES(0.0);
}

int64_t nix_get_int(nix_c_context * context, const nix_value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_in(value);
        assert(v.type() == nix::nInt);
        return v.integer().value;
    }
    NIXC_CATCH_ERRS_RES(0);
}

ExternalValue * nix_get_external(nix_c_context * context, nix_value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_out(value);
        assert(v.type() == nix::nExternal);
        return (ExternalValue *) v.external();
    }
    NIXC_CATCH_ERRS_NULL;
}

nix_value * nix_get_list_byidx(nix_c_context * context, const nix_value * value, EvalState * state, unsigned int ix)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_in(value);
        assert(v.type() == nix::nList);
        if (ix >= v.listSize()) {
            nix_set_err_msg(context, NIX_ERR_KEY, "list index out of bounds");
            return nullptr;
        }
        auto * p = v.listView()[ix];
        nix_gc_incref(nullptr, p);
        if (p != nullptr)
            state->state.forceValue(*p, nix::noPos);
        return as_nix_value_ptr(p);
    }
    NIXC_CATCH_ERRS_NULL
}

nix_value *
nix_get_list_byidx_lazy(nix_c_context * context, const nix_value * value, EvalState * state, unsigned int ix)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_in(value);
        assert(v.type() == nix::nList);
        if (ix >= v.listSize()) {
            nix_set_err_msg(context, NIX_ERR_KEY, "list index out of bounds");
            return nullptr;
        }
        auto * p = v.listView()[ix];
        nix_gc_incref(nullptr, p);
        // Note: intentionally NOT calling forceValue() to keep the element lazy
        return as_nix_value_ptr(p);
    }
    NIXC_CATCH_ERRS_NULL
}

nix_value * nix_get_attr_byname(nix_c_context * context, const nix_value * value, EvalState * state, const char * name)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_in(value);
        assert(v.type() == nix::nAttrs);
        nix::Symbol s = state->state.symbols.create(name);
        auto attr = v.attrs()->get(s);
        if (attr) {
            nix_gc_incref(nullptr, attr->value);
            state->state.forceValue(*attr->value, nix::noPos);
            return as_nix_value_ptr(attr->value);
        }
        nix_set_err_msg(context, NIX_ERR_KEY, "missing attribute");
        return nullptr;
    }
    NIXC_CATCH_ERRS_NULL
}

nix_value *
nix_get_attr_byname_lazy(nix_c_context * context, const nix_value * value, EvalState * state, const char * name)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_in(value);
        assert(v.type() == nix::nAttrs);
        nix::Symbol s = state->state.symbols.create(name);
        auto attr = v.attrs()->get(s);
        if (attr) {
            nix_gc_incref(nullptr, attr->value);
            // Note: intentionally NOT calling forceValue() to keep the attribute lazy
            return as_nix_value_ptr(attr->value);
        }
        nix_set_err_msg(context, NIX_ERR_KEY, "missing attribute");
        return nullptr;
    }
    NIXC_CATCH_ERRS_NULL
}

bool nix_has_attr_byname(nix_c_context * context, const nix_value * value, EvalState * state, const char * name)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_in(value);
        assert(v.type() == nix::nAttrs);
        nix::Symbol s = state->state.symbols.create(name);
        auto attr = v.attrs()->get(s);
        if (attr)
            return true;
        return false;
    }
    NIXC_CATCH_ERRS_RES(false);
}

static void collapse_attrset_layer_chain_if_needed(nix::Value & v, EvalState * state)
{
    auto & attrs = *v.attrs();
    if (attrs.isLayered()) {
        auto bindings = state->state.buildBindings(attrs.size());
        std::ranges::copy(attrs, std::back_inserter(bindings));
        v.mkAttrs(bindings);
    }
}

nix_value *
nix_get_attr_byidx(nix_c_context * context, nix_value * value, EvalState * state, unsigned int i, const char ** name)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_in(value);
        collapse_attrset_layer_chain_if_needed(v, state);
        if (i >= v.attrs()->size()) {
            nix_set_err_msg(context, NIX_ERR_KEY, "attribute index out of bounds");
            return nullptr;
        }
        const nix::Attr & a = (*v.attrs())[i];
        *name = state->state.symbols[a.name].c_str();
        nix_gc_incref(nullptr, a.value);
        state->state.forceValue(*a.value, nix::noPos);
        return as_nix_value_ptr(a.value);
    }
    NIXC_CATCH_ERRS_NULL
}

nix_value * nix_get_attr_byidx_lazy(
    nix_c_context * context, nix_value * value, EvalState * state, unsigned int i, const char ** name)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_in(value);
        collapse_attrset_layer_chain_if_needed(v, state);
        if (i >= v.attrs()->size()) {
            nix_set_err_msg(context, NIX_ERR_KEY, "attribute index out of bounds (Nix C API contract violation)");
            return nullptr;
        }
        const nix::Attr & a = (*v.attrs())[i];
        *name = state->state.symbols[a.name].c_str();
        nix_gc_incref(nullptr, a.value);
        // Note: intentionally NOT calling forceValue() to keep the attribute lazy
        return as_nix_value_ptr(a.value);
    }
    NIXC_CATCH_ERRS_NULL
}

const char * nix_get_attr_name_byidx(nix_c_context * context, nix_value * value, EvalState * state, unsigned int i)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_in(value);
        collapse_attrset_layer_chain_if_needed(v, state);
        if (i >= v.attrs()->size()) {
            nix_set_err_msg(context, NIX_ERR_KEY, "attribute index out of bounds (Nix C API contract violation)");
            return nullptr;
        }
        const nix::Attr & a = (*v.attrs())[i];
        return state->state.symbols[a.name].c_str();
    }
    NIXC_CATCH_ERRS_NULL
}

nix_err nix_init_bool(nix_c_context * context, nix_value * value, bool b)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_out(value);
        v.mkBool(b);
    }
    NIXC_CATCH_ERRS
}

// todo string context
nix_err nix_init_string(nix_c_context * context, nix_value * value, const char * str)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_out(value);
        v.mkString(std::string_view(str));
    }
    NIXC_CATCH_ERRS
}

nix_err nix_init_path_string(nix_c_context * context, EvalState * s, nix_value * value, const char * str)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_out(value);
        v.mkPath(s->state.rootPath(nix::CanonPath(str)));
    }
    NIXC_CATCH_ERRS
}

nix_err nix_init_float(nix_c_context * context, nix_value * value, double d)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_out(value);
        v.mkFloat(d);
    }
    NIXC_CATCH_ERRS
}

nix_err nix_init_int(nix_c_context * context, nix_value * value, int64_t i)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_out(value);
        v.mkInt(i);
    }
    NIXC_CATCH_ERRS
}

nix_err nix_init_null(nix_c_context * context, nix_value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_out(value);
        v.mkNull();
    }
    NIXC_CATCH_ERRS
}

nix_err nix_init_apply(nix_c_context * context, nix_value * value, nix_value * fn, nix_value * arg)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
        auto & f = check_value_not_null(fn);
        auto & a = check_value_not_null(arg);
        v.mkApp(&f, &a);
    }
    NIXC_CATCH_ERRS
}

nix_err nix_init_external(nix_c_context * context, nix_value * value, ExternalValue * val)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_out(value);
        auto r = (nix::ExternalValueBase *) val;
        v.mkExternal(r);
    }
    NIXC_CATCH_ERRS
}

ListBuilder * nix_make_list_builder(nix_c_context * context, EvalState * state, size_t capacity)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto builder = state->state.buildList(capacity);
        return new
#if NIX_USE_BOEHMGC
            (NoGC)
#endif
                ListBuilder{std::move(builder)};
    }
    NIXC_CATCH_ERRS_NULL
}

nix_err
nix_list_builder_insert(nix_c_context * context, ListBuilder * list_builder, unsigned int index, nix_value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & e = check_value_not_null(value);
        list_builder->builder[index] = &e;
    }
    NIXC_CATCH_ERRS
}

void nix_list_builder_free(ListBuilder * list_builder)
{
#if NIX_USE_BOEHMGC
    GC_FREE(list_builder);
#else
    delete list_builder;
#endif
}

nix_err nix_make_list(nix_c_context * context, ListBuilder * list_builder, nix_value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_out(value);
        v.mkList(list_builder->builder);
    }
    NIXC_CATCH_ERRS
}

nix_err nix_init_primop(nix_c_context * context, nix_value * value, PrimOp * p)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_out(value);
        v.mkPrimOp((nix::PrimOp *) p);
    }
    NIXC_CATCH_ERRS
}

nix_err nix_copy_value(nix_c_context * context, nix_value * value, const nix_value * source)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_out(value);
        auto & s = check_value_in(source);
        v = s;
    }
    NIXC_CATCH_ERRS
}

nix_err nix_make_attrs(nix_c_context * context, nix_value * value, BindingsBuilder * b)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_out(value);
        v.mkAttrs(b->builder);
    }
    NIXC_CATCH_ERRS
}

BindingsBuilder * nix_make_bindings_builder(nix_c_context * context, EvalState * state, size_t capacity)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto bb = state->state.buildBindings(capacity);
        return new
#if NIX_USE_BOEHMGC
            (NoGC)
#endif
                BindingsBuilder{std::move(bb)};
    }
    NIXC_CATCH_ERRS_NULL
}

nix_err nix_bindings_builder_insert(nix_c_context * context, BindingsBuilder * bb, const char * name, nix_value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
        nix::Symbol s = bb->builder.symbols.get().create(name);
        bb->builder.insert(s, &v);
    }
    NIXC_CATCH_ERRS
}

void nix_bindings_builder_free(BindingsBuilder * bb)
{
#if NIX_USE_BOEHMGC
    GC_FREE((nix::BindingsBuilder *) bb);
#else
    delete (nix::BindingsBuilder *) bb;
#endif
}

nix_realised_string * nix_string_realise(nix_c_context * context, EvalState * state, nix_value * value, bool isIFD)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_in(value);
        nix::StorePathSet storePaths;
        auto s = state->state.realiseString(v, &storePaths, isIFD);

        // Convert to the C API StorePath type and convert to vector for index-based access
        std::vector<StorePath> vec;
        for (auto & sp : storePaths) {
            vec.push_back(StorePath{sp});
        }

        return new nix_realised_string{.str = s, .storePaths = vec};
    }
    NIXC_CATCH_ERRS_NULL
}

void nix_realised_string_free(nix_realised_string * s)
{
    delete s;
}

size_t nix_realised_string_get_buffer_size(nix_realised_string * s)
{
    return s->str.size();
}

const char * nix_realised_string_get_buffer_start(nix_realised_string * s)
{
    return s->str.data();
}

size_t nix_realised_string_get_store_path_count(nix_realised_string * s)
{
    return s->storePaths.size();
}

const StorePath * nix_realised_string_get_store_path(nix_realised_string * s, size_t i)
{
    return &s->storePaths[i];
}

} // extern "C"
