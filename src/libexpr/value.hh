#pragma once
///@file

#include <cassert>
#include <climits>
#include <span>

#include "symbol-table.hh"
#include "value/context.hh"
#include "source-path.hh"
#include "print-options.hh"

#if HAVE_BOEHMGC
#include <gc/gc_allocator.h>
#endif
#include <nlohmann/json_fwd.hpp>

namespace nix {

struct Value;
class BindingsBuilder;


typedef enum {
    tUninitialized = 0,
    tInt = 1,
    tBool,
    tString,
    tPath,
    tNull,
    tAttrs,
    tList1,
    tList2,
    tListN,
    tThunk,
    tApp,
    tLambda,
    tPrimOp,
    tPrimOpApp,
    tExternal,
    tFloat
} InternalType;

/**
 * This type abstracts over all actual value types in the language,
 * grouping together implementation details like tList*, different function
 * types, and types in non-normal form (so thunks and co.)
 */
typedef enum {
    nThunk,
    nInt,
    nFloat,
    nBool,
    nString,
    nPath,
    nNull,
    nAttrs,
    nList,
    nFunction,
    nExternal
} ValueType;

class Bindings;
struct Env;
struct Expr;
struct ExprLambda;
struct ExprBlackHole;
struct PrimOp;
class Symbol;
class PosIdx;
struct Pos;
class StorePath;
class EvalState;
class XMLWriter;
class Printer;

typedef int64_t NixInt;
typedef double NixFloat;

/**
 * External values must descend from ExternalValueBase, so that
 * type-agnostic nix functions (e.g. showType) can be implemented
 */
class ExternalValueBase
{
    friend std::ostream & operator << (std::ostream & str, const ExternalValueBase & v);
    friend class Printer;
    protected:
    /**
     * Print out the value
     */
    virtual std::ostream & print(std::ostream & str) const = 0;

    public:
    /**
     * Return a simple string describing the type
     */
    virtual std::string showType() const = 0;

    /**
     * Return a string to be used in builtins.typeOf
     */
    virtual std::string typeOf() const = 0;

    /**
     * Coerce the value to a string. Defaults to uncoercable, i.e. throws an
     * error.
     */
    virtual std::string coerceToString(EvalState & state, const PosIdx & pos, NixStringContext & context, bool copyMore, bool copyToStore) const;

    /**
     * Compare to another value of the same type. Defaults to uncomparable,
     * i.e. always false.
     */
    virtual bool operator ==(const ExternalValueBase & b) const;

    /**
     * Print the value as JSON. Defaults to unconvertable, i.e. throws an error
     */
    virtual nlohmann::json printValueAsJSON(EvalState & state, bool strict,
        NixStringContext & context, bool copyToStore = true) const;

    /**
     * Print the value as XML. Defaults to unevaluated
     */
    virtual void printValueAsXML(EvalState & state, bool strict, bool location,
        XMLWriter & doc, NixStringContext & context, PathSet & drvsSeen,
        const PosIdx pos) const;

    virtual ~ExternalValueBase()
    {
    };
};

std::ostream & operator << (std::ostream & str, const ExternalValueBase & v);


class ListBuilder
{
    const size_t size;
    Value * inlineElems[2] = {nullptr, nullptr};
public:
    Value * * elems;
    ListBuilder(EvalState & state, size_t size);

    ListBuilder(ListBuilder && x)
        : size(x.size)
        , inlineElems{x.inlineElems[0], x.inlineElems[1]}
        , elems(size <= 2 ? inlineElems : x.elems)
    { }

    Value * & operator [](size_t n)
    {
        return elems[n];
    }

    typedef Value * * iterator;

    iterator begin() { return &elems[0]; }
    iterator end() { return &elems[size]; }

    friend struct Value;
};


struct Value
{
private:
    InternalType internalType = tUninitialized;

    friend std::string showType(const Value & v);

public:

    void print(EvalState &state, std::ostream &str, PrintOptions options = PrintOptions {});

    // Functions needed to distinguish the type
    // These should be removed eventually, by putting the functionality that's
    // needed by callers into methods of this type

    // type() == nThunk
    inline bool isThunk() const { return internalType == tThunk; };
    inline bool isApp() const { return internalType == tApp; };
    inline bool isBlackhole() const;

    // type() == nFunction
    inline bool isLambda() const { return internalType == tLambda; };
    inline bool isPrimOp() const { return internalType == tPrimOp; };
    inline bool isPrimOpApp() const { return internalType == tPrimOpApp; };

    /**
     * Strings in the evaluator carry a so-called `context` which
     * is a list of strings representing store paths.  This is to
     * allow users to write things like
     *
     *   "--with-freetype2-library=" + freetype + "/lib"
     *
     * where `freetype` is a derivation (or a source to be copied
     * to the store).  If we just concatenated the strings without
     * keeping track of the referenced store paths, then if the
     * string is used as a derivation attribute, the derivation
     * will not have the correct dependencies in its inputDrvs and
     * inputSrcs.

     * The semantics of the context is as follows: when a string
     * with context C is used as a derivation attribute, then the
     * derivations in C will be added to the inputDrvs of the
     * derivation, and the other store paths in C will be added to
     * the inputSrcs of the derivations.

     * For canonicity, the store paths should be in sorted order.
     */
    struct StringWithContext {
        const char * c_str;
        const char * * context; // must be in sorted order
    };

    struct Path {
        SourceAccessor * accessor;
        const char * path;
    };

    struct ClosureThunk {
        Env * env;
        Expr * expr;
    };

    struct FunctionApplicationThunk {
        Value * left, * right;
    };

    struct Lambda {
        Env * env;
        ExprLambda * fun;
    };

    using Payload = union
    {
        NixInt integer;
        bool boolean;

        StringWithContext string;

        Path path;

        Bindings * attrs;
        struct {
            size_t size;
            Value * const * elems;
        } bigList;
        Value * smallList[2];
        ClosureThunk thunk;
        FunctionApplicationThunk app;
        Lambda lambda;
        PrimOp * primOp;
        FunctionApplicationThunk primOpApp;
        ExternalValueBase * external;
        NixFloat fpoint;
    };

    Payload payload;

    /**
     * Returns the normal type of a Value. This only returns nThunk if
     * the Value hasn't been forceValue'd
     *
     * @param invalidIsThunk Instead of aborting an an invalid (probably
     * 0, so uninitialized) internal type, return `nThunk`.
     */
    inline ValueType type(bool invalidIsThunk = false) const
    {
        switch (internalType) {
            case tUninitialized: break;
            case tInt: return nInt;
            case tBool: return nBool;
            case tString: return nString;
            case tPath: return nPath;
            case tNull: return nNull;
            case tAttrs: return nAttrs;
            case tList1: case tList2: case tListN: return nList;
            case tLambda: case tPrimOp: case tPrimOpApp: return nFunction;
            case tExternal: return nExternal;
            case tFloat: return nFloat;
            case tThunk: case tApp: return nThunk;
        }
        if (invalidIsThunk)
            return nThunk;
        else
            abort();
    }

    inline void finishValue(InternalType newType, Payload newPayload)
    {
        payload = newPayload;
        internalType = newType;
    }

    /**
     * A value becomes valid when it is initialized. We don't use this
     * in the evaluator; only in the bindings, where the slight extra
     * cost is warranted because of inexperienced callers.
     */
    inline bool isValid() const
    {
        return internalType != tUninitialized;
    }

    inline void mkInt(NixInt n)
    {
        finishValue(tInt, { .integer = n });
    }

    inline void mkBool(bool b)
    {
        finishValue(tBool, { .boolean = b });
    }

    inline void mkString(const char * s, const char * * context = 0)
    {
        finishValue(tString, { .string = { .c_str = s, .context = context } });
    }

    void mkString(std::string_view s);

    void mkString(std::string_view s, const NixStringContext & context);

    void mkStringMove(const char * s, const NixStringContext & context);

    inline void mkString(const Symbol & s)
    {
        mkString(((const std::string &) s).c_str());
    }

    void mkPath(const SourcePath & path);
    void mkPath(std::string_view path);

    inline void mkPath(SourceAccessor * accessor, const char * path)
    {
        finishValue(tPath, { .path = { .accessor = accessor, .path = path } });
    }

    inline void mkNull()
    {
        finishValue(tNull, {});
    }

    inline void mkAttrs(Bindings * a)
    {
        finishValue(tAttrs, { .attrs = a });
    }

    Value & mkAttrs(BindingsBuilder & bindings);

    void mkList(const ListBuilder & builder)
    {
        if (builder.size == 1)
            finishValue(tList1, { .smallList = { builder.inlineElems[0] } });
        else if (builder.size == 2)
            finishValue(tList2, { .smallList = { builder.inlineElems[0], builder.inlineElems[1] } });
        else
            finishValue(tListN, { .bigList = { .size = builder.size, .elems = builder.elems } });
    }

    inline void mkThunk(Env * e, Expr * ex)
    {
        finishValue(tThunk, { .thunk = { .env = e, .expr = ex } });
    }

    inline void mkApp(Value * l, Value * r)
    {
        finishValue(tApp, { .app = { .left = l, .right = r } });
    }

    inline void mkLambda(Env * e, ExprLambda * f)
    {
        finishValue(tLambda, { .lambda = { .env = e, .fun = f } });
    }

    inline void mkBlackhole();

    void mkPrimOp(PrimOp * p);

    inline void mkPrimOpApp(Value * l, Value * r)
    {
        finishValue(tPrimOpApp, { .primOpApp = { .left = l, .right = r } });
    }

    /**
     * For a `tPrimOpApp` value, get the original `PrimOp` value.
     */
    const PrimOp * primOpAppPrimOp() const;

    inline void mkExternal(ExternalValueBase * e)
    {
        finishValue(tExternal, { .external = e });
    }

    inline void mkFloat(NixFloat n)
    {
        finishValue(tFloat, { .fpoint = n });
    }

    bool isList() const
    {
        return internalType == tList1 || internalType == tList2 || internalType == tListN;
    }

    Value * const * listElems()
    {
        return internalType == tList1 || internalType == tList2 ? payload.smallList : payload.bigList.elems;
    }

    std::span<Value * const> listItems() const
    {
        assert(isList());
        return std::span<Value * const>(listElems(), listSize());
    }

    Value * const * listElems() const
    {
        return internalType == tList1 || internalType == tList2 ? payload.smallList : payload.bigList.elems;
    }

    size_t listSize() const
    {
        return internalType == tList1 ? 1 : internalType == tList2 ? 2 : payload.bigList.size;
    }

    PosIdx determinePos(const PosIdx pos) const;

    /**
     * Check whether forcing this value requires a trivial amount of
     * computation. In particular, function applications are
     * non-trivial.
     */
    bool isTrivial() const;

    SourcePath path() const
    {
        assert(internalType == tPath);
        return SourcePath(
            ref(payload.path.accessor->shared_from_this()),
            CanonPath(CanonPath::unchecked_t(), payload.path.path));
    }

    std::string_view string_view() const
    {
        assert(internalType == tString);
        return std::string_view(payload.string.c_str);
    }

    const char * const c_str() const
    {
        assert(internalType == tString);
        return payload.string.c_str;
    }

    const char * * context() const
    {
        return payload.string.context;
    }

    ExternalValueBase * external() const
    { return payload.external; }

    const Bindings * attrs() const
    { return payload.attrs; }

    const PrimOp * primOp() const
    { return payload.primOp; }

    bool boolean() const
    { return payload.boolean; }

    NixInt integer() const
    { return payload.integer; }

    NixFloat fpoint() const
    { return payload.fpoint; }
};


extern ExprBlackHole eBlackHole;

bool Value::isBlackhole() const
{
    return internalType == tThunk && payload.thunk.expr == (Expr*) &eBlackHole;
}

void Value::mkBlackhole()
{
    mkThunk(nullptr, (Expr *) &eBlackHole);
}


#if HAVE_BOEHMGC
typedef std::vector<Value *, traceable_allocator<Value *>> ValueVector;
typedef std::map<Symbol, Value *, std::less<Symbol>, traceable_allocator<std::pair<const Symbol, Value *>>> ValueMap;
typedef std::map<Symbol, ValueVector, std::less<Symbol>, traceable_allocator<std::pair<const Symbol, ValueVector>>> ValueVectorMap;
#else
typedef std::vector<Value *> ValueVector;
typedef std::map<Symbol, Value *> ValueMap;
typedef std::map<Symbol, ValueVector> ValueVectorMap;
#endif


/**
 * A value allocated in traceable memory.
 */
typedef std::shared_ptr<Value *> RootValue;

RootValue allocRootValue(Value * v);

}
