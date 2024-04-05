#include "attr-set.hh"
#include "config.hh"
#include "eval.hh"
#include "globals.hh"
#include "primops.hh"
#include "value.hh"

#include "nix_api_expr.h"
#include "nix_api_expr_internal.h"
#include "nix_api_util.h"
#include "nix_api_util_internal.h"
#include "nix_api_value.h"

#ifdef HAVE_BOEHMGC
# include "gc/gc.h"
# define GC_INCLUDE_NEW 1
# include "gc_cpp.h"
#endif

// Helper function to throw an exception if value is null
static const nix::Value & check_value_not_null(const Value * value)
{
    if (!value) {
        throw std::runtime_error("Value is null");
    }
    return *((const nix::Value *) value);
}

static nix::Value & check_value_not_null(Value * value)
{
    if (!value) {
        throw std::runtime_error("Value is null");
    }
    return *((nix::Value *) value);
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
    f(userdata, &ctx, (EvalState *) &state, (Value **) args, (Value *) &v);
    /* TODO: In the future, this should throw different errors depending on the error code */
    if (ctx.last_err_code != NIX_OK)
        state.error<nix::EvalError>("Error from builtin function: %s", *ctx.last_err).atPos(pos).debugThrow();
}

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
#ifdef HAVE_BOEHMGC
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

Value * nix_alloc_value(nix_c_context * context, EvalState * state)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        Value * res = state->state.allocValue();
        nix_gc_incref(nullptr, res);
        return res;
    }
    NIXC_CATCH_ERRS_NULL
}

ValueType nix_get_type(nix_c_context * context, const Value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
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

const char * nix_get_typename(nix_c_context * context, const Value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
        auto s = nix::showType(v);
        return strdup(s.c_str());
    }
    NIXC_CATCH_ERRS_NULL
}

bool nix_get_bool(nix_c_context * context, const Value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
        assert(v.type() == nix::nBool);
        return v.boolean;
    }
    NIXC_CATCH_ERRS_RES(false);
}

const char * nix_get_string(nix_c_context * context, const Value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
        assert(v.type() == nix::nString);
        return v.c_str();
    }
    NIXC_CATCH_ERRS_NULL
}

const char * nix_get_path_string(nix_c_context * context, const Value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
        assert(v.type() == nix::nPath);
        // NOTE (from @yorickvP)
        // v._path.path should work but may not be how Eelco intended it.
        // Long-term this function should be rewritten to copy some data into a
        // user-allocated string.
        // We could use v.path().to_string().c_str(), but I'm concerned this
        // crashes. Looks like .path() allocates a CanonPath with a copy of the
        // string, then it gets the underlying data from that.
        return v._path.path;
    }
    NIXC_CATCH_ERRS_NULL
}

unsigned int nix_get_list_size(nix_c_context * context, const Value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
        assert(v.type() == nix::nList);
        return v.listSize();
    }
    NIXC_CATCH_ERRS_RES(0);
}

unsigned int nix_get_attrs_size(nix_c_context * context, const Value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
        assert(v.type() == nix::nAttrs);
        return v.attrs->size();
    }
    NIXC_CATCH_ERRS_RES(0);
}

double nix_get_float(nix_c_context * context, const Value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
        assert(v.type() == nix::nFloat);
        return v.fpoint;
    }
    NIXC_CATCH_ERRS_RES(0.0);
}

int64_t nix_get_int(nix_c_context * context, const Value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
        assert(v.type() == nix::nInt);
        return v.integer;
    }
    NIXC_CATCH_ERRS_RES(0);
}

ExternalValue * nix_get_external(nix_c_context * context, Value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
        assert(v.type() == nix::nExternal);
        return (ExternalValue *) v.external;
    }
    NIXC_CATCH_ERRS_NULL;
}

Value * nix_get_list_byidx(nix_c_context * context, const Value * value, EvalState * state, unsigned int ix)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
        assert(v.type() == nix::nList);
        auto * p = v.listElems()[ix];
        nix_gc_incref(nullptr, p);
        if (p != nullptr)
            state->state.forceValue(*p, nix::noPos);
        return (Value *) p;
    }
    NIXC_CATCH_ERRS_NULL
}

Value * nix_get_attr_byname(nix_c_context * context, const Value * value, EvalState * state, const char * name)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
        assert(v.type() == nix::nAttrs);
        nix::Symbol s = state->state.symbols.create(name);
        auto attr = v.attrs->get(s);
        if (attr) {
            nix_gc_incref(nullptr, attr->value);
            state->state.forceValue(*attr->value, nix::noPos);
            return attr->value;
        }
        nix_set_err_msg(context, NIX_ERR_KEY, "missing attribute");
        return nullptr;
    }
    NIXC_CATCH_ERRS_NULL
}

bool nix_has_attr_byname(nix_c_context * context, const Value * value, EvalState * state, const char * name)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
        assert(v.type() == nix::nAttrs);
        nix::Symbol s = state->state.symbols.create(name);
        auto attr = v.attrs->get(s);
        if (attr)
            return true;
        return false;
    }
    NIXC_CATCH_ERRS_RES(false);
}

Value *
nix_get_attr_byidx(nix_c_context * context, const Value * value, EvalState * state, unsigned int i, const char ** name)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
        const nix::Attr & a = (*v.attrs)[i];
        *name = ((const std::string &) (state->state.symbols[a.name])).c_str();
        nix_gc_incref(nullptr, a.value);
        state->state.forceValue(*a.value, nix::noPos);
        return a.value;
    }
    NIXC_CATCH_ERRS_NULL
}

const char * nix_get_attr_name_byidx(nix_c_context * context, const Value * value, EvalState * state, unsigned int i)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
        const nix::Attr & a = (*v.attrs)[i];
        return ((const std::string &) (state->state.symbols[a.name])).c_str();
    }
    NIXC_CATCH_ERRS_NULL
}

nix_err nix_init_bool(nix_c_context * context, Value * value, bool b)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
        v.mkBool(b);
    }
    NIXC_CATCH_ERRS
}

// todo string context
nix_err nix_init_string(nix_c_context * context, Value * value, const char * str)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
        v.mkString(std::string_view(str));
    }
    NIXC_CATCH_ERRS
}

nix_err nix_init_path_string(nix_c_context * context, EvalState * s, Value * value, const char * str)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
        v.mkPath(s->state.rootPath(nix::CanonPath(str)));
    }
    NIXC_CATCH_ERRS
}

nix_err nix_init_float(nix_c_context * context, Value * value, double d)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
        v.mkFloat(d);
    }
    NIXC_CATCH_ERRS
}

nix_err nix_init_int(nix_c_context * context, Value * value, int64_t i)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
        v.mkInt(i);
    }
    NIXC_CATCH_ERRS
}

nix_err nix_init_null(nix_c_context * context, Value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
        v.mkNull();
    }
    NIXC_CATCH_ERRS
}

nix_err nix_init_external(nix_c_context * context, Value * value, ExternalValue * val)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
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
#if HAVE_BOEHMGC
            (NoGC)
#endif
                ListBuilder{std::move(builder)};
    }
    NIXC_CATCH_ERRS_NULL
}

nix_err nix_list_builder_insert(nix_c_context * context, ListBuilder * list_builder, unsigned int index, Value * value)
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
#if HAVE_BOEHMGC
    GC_FREE(list_builder);
#else
    delete list_builder;
#endif
}

nix_err nix_make_list(nix_c_context * context, ListBuilder * list_builder, Value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
        v.mkList(list_builder->builder);
    }
    NIXC_CATCH_ERRS
}

nix_err nix_init_primop(nix_c_context * context, Value * value, PrimOp * p)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
        v.mkPrimOp((nix::PrimOp *) p);
    }
    NIXC_CATCH_ERRS
}

nix_err nix_copy_value(nix_c_context * context, Value * value, Value * source)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
        auto & s = check_value_not_null(source);
        v = s;
    }
    NIXC_CATCH_ERRS
}

nix_err nix_make_attrs(nix_c_context * context, Value * value, BindingsBuilder * b)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
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
#if HAVE_BOEHMGC
            (NoGC)
#endif
                BindingsBuilder{std::move(bb)};
    }
    NIXC_CATCH_ERRS_NULL
}

nix_err nix_bindings_builder_insert(nix_c_context * context, BindingsBuilder * bb, const char * name, Value * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto & v = check_value_not_null(value);
        nix::Symbol s = bb->builder.state.symbols.create(name);
        bb->builder.insert(s, &v);
    }
    NIXC_CATCH_ERRS
}

void nix_bindings_builder_free(BindingsBuilder * bb)
{
#if HAVE_BOEHMGC
    GC_FREE((nix::BindingsBuilder *) bb);
#else
    delete (nix::BindingsBuilder *) bb;
#endif
}
