#pragma once
///@file

#include <cassert>
#include <span>

#include "eval-gc.hh"
#include "symbol-table.hh"
#include "value/context.hh"
#include "source-path.hh"
#include "print-options.hh"
#include "checked-arithmetic.hh"

#include <nlohmann/json_fwd.hpp>

namespace nix {

struct Value;
class BindingsBuilder;

typedef enum {
    /* Unfinished values. */
    tUninitialized = 0,
    tThunk,
    tApp,
    tPending,
    tAwaited,

    /* Finished values. */
    tInt = 32, // Do not move tInt (see isFinished()).
    tBool,
    tString,
    tPath,
    tNull,
    tAttrs,
    tList1,
    tList2,
    tListN,
    tLambda,
    tPrimOp,
    tPrimOpApp,
    tExternal,
    tFloat,
    tFailed,
} InternalType;

/**
 * Return true if `type` denotes a "finished" value, i.e. a weak-head
 * normal form.
 *
 * Note that tPrimOpApp is considered "finished" because it represents
 * a primop call with an incomplete number of arguments, and therefore
 * cannot be evaluated further.
 */
inline bool isFinished(InternalType type)
{
    return type >= tInt;
}

/**
 * This type abstracts over all actual value types in the language,
 * grouping together implementation details like tList*, different function
 * types, and types in non-normal form (so thunks and co.)
 */
typedef enum {
    nThunk,
    nFailed,
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

using NixInt = checked::Checked<int64_t>;
using NixFloat = double;

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
    virtual bool operator ==(const ExternalValueBase & b) const noexcept;

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

    // NOTE: Can be noexcept because we are just copying integral values and
    // raw pointers.
    ListBuilder(ListBuilder && x) noexcept
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
    std::atomic<InternalType> internalType{tUninitialized};

    friend std::string showType(const Value & v);
    friend class EvalState;

public:

    Value()
        : internalType(tUninitialized)
    { }

    Value(const Value & v)
    { *this = v; }

    /**
     * Copy a value. This is not allowed to be a thunk to avoid
     * accidental work duplication.
     */
    Value & operator =(const Value & v)
    {
        auto type = v.internalType.load(std::memory_order_acquire);
        //debug("ASSIGN %x %d %d", this, internalType, type);
        if (!nix::isFinished(type)) {
            printError("UNEXPECTED TYPE %x %x %d %s", this, &v, type, showType(v));
            abort();
        }
        finishValue(type, v.payload);
        return *this;
    }

    void print(EvalState &state, std::ostream &str, PrintOptions options = PrintOptions {});

    inline bool isFinished() const
    {
        return nix::isFinished(internalType.load(std::memory_order_acquire));
    }

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

    struct Failed
    {
        std::exception_ptr ex;
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
        Failed * failed;
    };

    Payload payload;

    /**
     * Returns the normal type of a Value. This only returns nThunk if
     * the Value hasn't been forceValue'd
     */
    inline ValueType type() const
    {
        switch (internalType) {
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
            case tFailed: return nFailed;
            case tThunk: case tApp: case tPending: case tAwaited: return nThunk;
            case tUninitialized:
            default:
                unreachable();
        }
    }

    /**
     * Finish a pending thunk, waking up any threads that are waiting
     * on it.
     */
    inline void finishValue(InternalType newType, Payload newPayload)
    {
        debug("FINISH %x %d %d", this, internalType, newType);
        payload = newPayload;

        auto oldType = internalType.exchange(newType, std::memory_order_release);

        if (oldType == tUninitialized)
            // Uninitialized value; nothing to do.
            ;
        else if (oldType == tPending)
            // Nothing to do; no thread is waiting on this thunk.
            ;
        else if (oldType == tAwaited)
            // Slow path: wake up the threads that are waiting on this
            // thunk.
            notifyWaiters();
        else {
            printError("BAD FINISH %x %d %d", this, oldType, newType);
            abort();
        }
    }

    inline void setThunk(InternalType newType, Payload newPayload)
    {
        payload = newPayload;

        auto oldType = internalType.exchange(newType, std::memory_order_release);

        if (oldType != tUninitialized) {
            printError("BAD SET THUNK %x %d %d", this, oldType, newType);
            abort();
        }
    }

    inline void reset()
    {
        auto oldType = internalType.exchange(tUninitialized, std::memory_order_relaxed);
        debug("RESET %x %d", this, oldType);
        if (oldType == tPending || oldType == tAwaited) {
            printError("BAD RESET %x %d", this, oldType);
            abort();
        }
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

    /**
     * Wake up any threads that are waiting on this value.
     * FIXME: this should be in EvalState.
     */
    void notifyWaiters();

    inline void mkInt(NixInt n)
    {
        finishValue(tInt, { .integer = n });
    }

    inline void mkInt(NixInt::Inner n)
    {
        mkInt(NixInt{n});
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

    inline void mkString(const SymbolStr & s)
    {
        mkString(s.c_str());
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
        setThunk(tThunk, { .thunk = { .env = e, .expr = ex } });
    }

    inline void mkApp(Value * l, Value * r)
    {
        setThunk(tApp, { .app = { .left = l, .right = r } });
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

    void mkFailed()
    {
        finishValue(tFailed, { .failed = new Value::Failed { .ex = std::current_exception() } });
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
     * computation. A value is trivial if it's finished or if it's a
     * thunk whose expression is an attrset with no dynamic
     * attributes, a lambda or a list. Note that it's up to the caller
     * to check whether the members of those attrsets or lists must be
     * trivial.
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

    const char * c_str() const
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


typedef std::vector<Value *, traceable_allocator<Value *>> ValueVector;
typedef std::unordered_map<Symbol, Value *, std::hash<Symbol>, std::equal_to<Symbol>, traceable_allocator<std::pair<const Symbol, Value *>>> ValueMap;
typedef std::map<Symbol, ValueVector, std::less<Symbol>, traceable_allocator<std::pair<const Symbol, ValueVector>>> ValueVectorMap;


/**
 * A value allocated in traceable memory.
 */
typedef std::shared_ptr<Value *> RootValue;

RootValue allocRootValue(Value * v);

void forceNoNullByte(std::string_view s, std::function<Pos()> = nullptr);

}
