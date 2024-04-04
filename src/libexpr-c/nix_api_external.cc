#include "attr-set.hh"
#include "config.hh"
#include "eval.hh"
#include "globals.hh"
#include "value.hh"

#include "nix_api_expr.h"
#include "nix_api_expr_internal.h"
#include "nix_api_external.h"
#include "nix_api_util.h"
#include "nix_api_util_internal.h"
#include "nix_api_value.h"
#include "value/context.hh"

#include <nlohmann/json.hpp>

#ifdef HAVE_BOEHMGC
# include "gc/gc.h"
# define GC_INCLUDE_NEW 1
# include "gc_cpp.h"
#endif

void nix_set_string_return(nix_string_return * str, const char * c)
{
    str->str = c;
}

nix_err nix_external_print(nix_c_context * context, nix_printer * printer, const char * c)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        printer->s << c;
    }
    NIXC_CATCH_ERRS
}

nix_err nix_external_add_string_context(nix_c_context * context, nix_string_context * ctx, const char * c)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto r = nix::NixStringContextElem::parse(c);
        ctx->ctx.insert(r);
    }
    NIXC_CATCH_ERRS
}

class NixCExternalValue : public nix::ExternalValueBase
{
    NixCExternalValueDesc & desc;
    void * v;

public:
    NixCExternalValue(NixCExternalValueDesc & desc, void * v)
        : desc(desc)
        , v(v){};
    void * get_ptr()
    {
        return v;
    }
    /**
     * Print out the value
     */
    virtual std::ostream & print(std::ostream & str) const override
    {
        nix_printer p{str};
        desc.print(v, &p);
        return str;
    }

    /**
     * Return a simple string describing the type
     */
    virtual std::string showType() const override
    {
        nix_string_return res;
        desc.showType(v, &res);
        return std::move(res.str);
    }

    /**
     * Return a string to be used in builtins.typeOf
     */
    virtual std::string typeOf() const override
    {
        nix_string_return res;
        desc.typeOf(v, &res);
        return std::move(res.str);
    }

    /**
     * Coerce the value to a string.
     */
    virtual std::string coerceToString(
        nix::EvalState & state,
        const nix::PosIdx & pos,
        nix::NixStringContext & context,
        bool copyMore,
        bool copyToStore) const override
    {
        if (!desc.coerceToString) {
            return nix::ExternalValueBase::coerceToString(state, pos, context, copyMore, copyToStore);
        }
        nix_string_context ctx{context};
        nix_string_return res{""};
        // todo: pos, errors
        desc.coerceToString(v, &ctx, copyMore, copyToStore, &res);
        if (res.str.empty()) {
            return nix::ExternalValueBase::coerceToString(state, pos, context, copyMore, copyToStore);
        }
        return std::move(res.str);
    }

    /**
     * Compare to another value of the same type.
     */
    virtual bool operator==(const ExternalValueBase & b) const override
    {
        if (!desc.equal) {
            return false;
        }
        auto r = dynamic_cast<const NixCExternalValue *>(&b);
        if (!r)
            return false;
        return desc.equal(v, r->v);
    }

    /**
     * Print the value as JSON.
     */
    virtual nlohmann::json printValueAsJSON(
        nix::EvalState & state, bool strict, nix::NixStringContext & context, bool copyToStore = true) const override
    {
        if (!desc.printValueAsJSON) {
            return nix::ExternalValueBase::printValueAsJSON(state, strict, context, copyToStore);
        }
        nix_string_context ctx{context};
        nix_string_return res{""};
        desc.printValueAsJSON(v, (EvalState *) &state, strict, &ctx, copyToStore, &res);
        if (res.str.empty()) {
            return nix::ExternalValueBase::printValueAsJSON(state, strict, context, copyToStore);
        }
        return nlohmann::json::parse(res.str);
    }

    /**
     * Print the value as XML.
     */
    virtual void printValueAsXML(
        nix::EvalState & state,
        bool strict,
        bool location,
        nix::XMLWriter & doc,
        nix::NixStringContext & context,
        nix::PathSet & drvsSeen,
        const nix::PosIdx pos) const override
    {
        if (!desc.printValueAsXML) {
            return nix::ExternalValueBase::printValueAsXML(state, strict, location, doc, context, drvsSeen, pos);
        }
        nix_string_context ctx{context};
        desc.printValueAsXML(
            v, (EvalState *) &state, strict, location, &doc, &ctx, &drvsSeen,
            *reinterpret_cast<const uint32_t *>(&pos));
    }

    virtual ~NixCExternalValue() override{};
};

ExternalValue * nix_create_external_value(nix_c_context * context, NixCExternalValueDesc * desc, void * v)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto ret = new
#ifdef HAVE_BOEHMGC
            (GC)
#endif
                NixCExternalValue(*desc, v);
        nix_gc_incref(nullptr, ret);
        return (ExternalValue *) ret;
    }
    NIXC_CATCH_ERRS_NULL
}

void * nix_get_external_value_content(nix_c_context * context, ExternalValue * b)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        auto r = dynamic_cast<NixCExternalValue *>((nix::ExternalValueBase *) b);
        if (r)
            return r->get_ptr();
        return nullptr;
    }
    NIXC_CATCH_ERRS_NULL
}
