#pragma once
///@file

#include <cassert>
#include <span>
#include <type_traits>

#include "nix/expr/eval-gc.hh"
#include "nix/expr/value/context.hh"
#include "nix/util/source-path.hh"
#include "nix/expr/print-options.hh"
#include "nix/util/checked-arithmetic.hh"

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
    tListSmall,
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
    nExternal,
} ValueType;

class Bindings;
struct Env;
struct Expr;
struct ExprLambda;
struct ExprBlackHole;
struct PrimOp;
class Symbol;
class SymbolStr;
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
    friend std::ostream & operator<<(std::ostream & str, const ExternalValueBase & v);
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
    virtual std::string coerceToString(
        EvalState & state, const PosIdx & pos, NixStringContext & context, bool copyMore, bool copyToStore) const;

    /**
     * Compare to another value of the same type. Defaults to uncomparable,
     * i.e. always false.
     */
    virtual bool operator==(const ExternalValueBase & b) const noexcept;

    /**
     * Print the value as JSON. Defaults to unconvertable, i.e. throws an error
     */
    virtual nlohmann::json
    printValueAsJSON(EvalState & state, bool strict, NixStringContext & context, bool copyToStore = true) const;

    /**
     * Print the value as XML. Defaults to unevaluated
     */
    virtual void printValueAsXML(
        EvalState & state,
        bool strict,
        bool location,
        XMLWriter & doc,
        NixStringContext & context,
        PathSet & drvsSeen,
        const PosIdx pos) const;

    virtual ~ExternalValueBase() {};
};

std::ostream & operator<<(std::ostream & str, const ExternalValueBase & v);

class ListBuilder
{
    const size_t size;
    Value * inlineElems[2] = {nullptr, nullptr};
public:
    Value ** elems;
    ListBuilder(EvalState & state, size_t size);

    // NOTE: Can be noexcept because we are just copying integral values and
    // raw pointers.
    ListBuilder(ListBuilder && x) noexcept
        : size(x.size)
        , inlineElems{x.inlineElems[0], x.inlineElems[1]}
        , elems(size <= 2 ? inlineElems : x.elems)
    {
    }

    Value *& operator[](size_t n)
    {
        return elems[n];
    }

    typedef Value ** iterator;

    iterator begin()
    {
        return &elems[0];
    }
    iterator end()
    {
        return &elems[size];
    }

    friend struct Value;
};

namespace detail {

/**
 * Implementation mixin class for defining the public types
 * In can be inherited from by the actual ValueStorage implementations
 * for free due to Empty Base Class Optimization (EBCO).
 */
struct ValueBase
{
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
    struct StringWithContext
    {
        const char * c_str;
        const char ** context; // must be in sorted order
    };

    struct Path
    {
        SourceAccessor * accessor;
        const char * path;
    };

    struct Null
    {};

    struct ClosureThunk
    {
        Env * env;
        Expr * expr;
    };

    struct FunctionApplicationThunk
    {
        Value *left, *right;
    };

    /**
     * Like FunctionApplicationThunk, but must be a distinct type in order to
     * resolve overloads to `tPrimOpApp` instead of `tApp`.
     * This type helps with the efficient implementation of arity>=2 primop calls.
     */
    struct PrimOpApplicationThunk
    {
        Value *left, *right;
    };

    struct Lambda
    {
        Env * env;
        ExprLambda * fun;
    };

    using SmallList = std::array<Value *, 2>;

    struct List
    {
        size_t size;
        Value * const * elems;
    };
};

template<typename T>
struct PayloadTypeToInternalType
{};

/**
 * All stored types must be distinct (not type aliases) for the purposes of
 * overload resolution in setStorage. This ensures there's a bijection from
 * InternalType <-> C++ type.
 */
#define NIX_VALUE_STORAGE_FOR_EACH_FIELD(MACRO)                     \
    MACRO(NixInt, integer, tInt)                                    \
    MACRO(bool, boolean, tBool)                                     \
    MACRO(ValueBase::StringWithContext, string, tString)            \
    MACRO(ValueBase::Path, path, tPath)                             \
    MACRO(ValueBase::Null, null_, tNull)                            \
    MACRO(Bindings *, attrs, tAttrs)                                \
    MACRO(ValueBase::List, bigList, tListN)                         \
    MACRO(ValueBase::SmallList, smallList, tListSmall)              \
    MACRO(ValueBase::ClosureThunk, thunk, tThunk)                   \
    MACRO(ValueBase::FunctionApplicationThunk, app, tApp)           \
    MACRO(ValueBase::Lambda, lambda, tLambda)                       \
    MACRO(PrimOp *, primOp, tPrimOp)                                \
    MACRO(ValueBase::PrimOpApplicationThunk, primOpApp, tPrimOpApp) \
    MACRO(ExternalValueBase *, external, tExternal)                 \
    MACRO(NixFloat, fpoint, tFloat)

#define NIX_VALUE_PAYLOAD_TYPE(T, FIELD_NAME, DISCRIMINATOR) \
    template<>                                               \
    struct PayloadTypeToInternalType<T>                      \
    {                                                        \
        static constexpr InternalType value = DISCRIMINATOR; \
    };

NIX_VALUE_STORAGE_FOR_EACH_FIELD(NIX_VALUE_PAYLOAD_TYPE)

#undef NIX_VALUE_PAYLOAD_TYPE

template<typename T>
inline constexpr InternalType payloadTypeToInternalType = PayloadTypeToInternalType<T>::value;

}

/**
 * Discriminated union of types stored in the value.
 * The union discriminator is @ref InternalType enumeration.
 *
 * This class can be specialized with a non-type template parameter
 * of pointer size for more optimized data layouts on when pointer alignment
 * bits can be used for storing the discriminator.
 *
 * All specializations of this type need to implement getStorage, setStorage and
 * getInternalType methods.
 */
template<std::size_t ptrSize>
class ValueStorage : public detail::ValueBase
{
protected:
    using Payload = union
    {
#define NIX_VALUE_STORAGE_DEFINE_FIELD(T, FIELD_NAME, DISCRIMINATOR) T FIELD_NAME;
        NIX_VALUE_STORAGE_FOR_EACH_FIELD(NIX_VALUE_STORAGE_DEFINE_FIELD)
#undef NIX_VALUE_STORAGE_DEFINE_FIELD
    };

    Payload payload;

private:
    InternalType internalType = tUninitialized;

public:
#define NIX_VALUE_STORAGE_GET_IMPL(K, FIELD_NAME, DISCRIMINATOR) \
    void getStorage(K & val) const noexcept                      \
    {                                                            \
        assert(internalType == DISCRIMINATOR);                   \
        val = payload.FIELD_NAME;                                \
    }

#define NIX_VALUE_STORAGE_SET_IMPL(K, FIELD_NAME, DISCRIMINATOR) \
    void setStorage(K val) noexcept                              \
    {                                                            \
        payload.FIELD_NAME = val;                                \
        internalType = DISCRIMINATOR;                            \
    }

    NIX_VALUE_STORAGE_FOR_EACH_FIELD(NIX_VALUE_STORAGE_GET_IMPL)
    NIX_VALUE_STORAGE_FOR_EACH_FIELD(NIX_VALUE_STORAGE_SET_IMPL)

#undef NIX_VALUE_STORAGE_SET_IMPL
#undef NIX_VALUE_STORAGE_GET_IMPL
#undef NIX_VALUE_STORAGE_FOR_EACH_FIELD

    /** Get internal type currently occupying the storage. */
    InternalType getInternalType() const noexcept
    {
        return internalType;
    }
};

struct Value : public ValueStorage<sizeof(void *)>
{
    friend std::string showType(const Value & v);

    template<InternalType... discriminator>
    bool isa() const noexcept
    {
        return ((getInternalType() == discriminator) || ...);
    }

    template<typename T>
    T getStorage() const noexcept
    {
        if (getInternalType() != detail::payloadTypeToInternalType<T>) [[unlikely]]
            unreachable();
        T out;
        ValueStorage::getStorage(out);
        return out;
    }

public:

    /**
     * Never modify the backing `Value` object!
     */
    static Value * toPtr(SymbolStr str) noexcept;

    void print(EvalState & state, std::ostream & str, PrintOptions options = PrintOptions{});

    // Functions needed to distinguish the type
    // These should be removed eventually, by putting the functionality that's
    // needed by callers into methods of this type

    // type() == nThunk
    inline bool isThunk() const
    {
        return isa<tThunk>();
    };
    inline bool isApp() const
    {
        return isa<tApp>();
    };
    inline bool isBlackhole() const;

    // type() == nFunction
    inline bool isLambda() const
    {
        return isa<tLambda>();
    };
    inline bool isPrimOp() const
    {
        return isa<tPrimOp>();
    };
    inline bool isPrimOpApp() const
    {
        return isa<tPrimOpApp>();
    };

    /**
     * Returns the normal type of a Value. This only returns nThunk if
     * the Value hasn't been forceValue'd
     *
     * @param invalidIsThunk Instead of aborting an an invalid (probably
     * 0, so uninitialized) internal type, return `nThunk`.
     */
    inline ValueType type(bool invalidIsThunk = false) const
    {
        switch (getInternalType()) {
        case tUninitialized:
            break;
        case tInt:
            return nInt;
        case tBool:
            return nBool;
        case tString:
            return nString;
        case tPath:
            return nPath;
        case tNull:
            return nNull;
        case tAttrs:
            return nAttrs;
        case tListSmall:
        case tListN:
            return nList;
        case tLambda:
        case tPrimOp:
        case tPrimOpApp:
            return nFunction;
        case tExternal:
            return nExternal;
        case tFloat:
            return nFloat;
        case tThunk:
        case tApp:
            return nThunk;
        }
        if (invalidIsThunk)
            return nThunk;
        else
            unreachable();
    }

    /**
     * A value becomes valid when it is initialized. We don't use this
     * in the evaluator; only in the bindings, where the slight extra
     * cost is warranted because of inexperienced callers.
     */
    inline bool isValid() const noexcept
    {
        return !isa<tUninitialized>();
    }

    inline void mkInt(NixInt::Inner n) noexcept
    {
        mkInt(NixInt{n});
    }

    inline void mkInt(NixInt n) noexcept
    {
        setStorage(NixInt{n});
    }

    inline void mkBool(bool b) noexcept
    {
        setStorage(b);
    }

    inline void mkString(const char * s, const char ** context = 0) noexcept
    {
        setStorage(StringWithContext{.c_str = s, .context = context});
    }

    void mkString(std::string_view s);

    void mkString(std::string_view s, const NixStringContext & context);

    void mkStringMove(const char * s, const NixStringContext & context);

    void mkPath(const SourcePath & path);
    void mkPath(std::string_view path);

    inline void mkPath(SourceAccessor * accessor, const char * path) noexcept
    {
        setStorage(Path{.accessor = accessor, .path = path});
    }

    inline void mkNull() noexcept
    {
        setStorage(Null{});
    }

    inline void mkAttrs(Bindings * a) noexcept
    {
        setStorage(a);
    }

    Value & mkAttrs(BindingsBuilder & bindings);

    void mkList(const ListBuilder & builder) noexcept
    {
        if (builder.size == 1)
            setStorage(std::array<Value *, 2>{builder.inlineElems[0], nullptr});
        else if (builder.size == 2)
            setStorage(std::array<Value *, 2>{builder.inlineElems[0], builder.inlineElems[1]});
        else
            setStorage(List{.size = builder.size, .elems = builder.elems});
    }

    inline void mkThunk(Env * e, Expr * ex) noexcept
    {
        setStorage(ClosureThunk{.env = e, .expr = ex});
    }

    inline void mkApp(Value * l, Value * r) noexcept
    {
        setStorage(FunctionApplicationThunk{.left = l, .right = r});
    }

    inline void mkLambda(Env * e, ExprLambda * f) noexcept
    {
        setStorage(Lambda{.env = e, .fun = f});
    }

    inline void mkBlackhole();

    void mkPrimOp(PrimOp * p);

    inline void mkPrimOpApp(Value * l, Value * r) noexcept
    {
        setStorage(PrimOpApplicationThunk{.left = l, .right = r});
    }

    /**
     * For a `tPrimOpApp` value, get the original `PrimOp` value.
     */
    const PrimOp * primOpAppPrimOp() const;

    inline void mkExternal(ExternalValueBase * e) noexcept
    {
        setStorage(e);
    }

    inline void mkFloat(NixFloat n) noexcept
    {
        setStorage(n);
    }

    bool isList() const noexcept
    {
        return isa<tListSmall, tListN>();
    }

    std::span<Value * const> listItems() const noexcept
    {
        assert(isList());
        return std::span<Value * const>(listElems(), listSize());
    }

    Value * const * listElems() const noexcept
    {
        return isa<tListSmall>() ? payload.smallList.data() : getStorage<List>().elems;
    }

    size_t listSize() const noexcept
    {
        return isa<tListSmall>() ? (getStorage<SmallList>()[1] == nullptr ? 1 : 2) : getStorage<List>().size;
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
        return SourcePath(ref(pathAccessor()->shared_from_this()), CanonPath(CanonPath::unchecked_t(), pathStr()));
    }

    std::string_view string_view() const noexcept
    {
        return std::string_view(getStorage<StringWithContext>().c_str);
    }

    const char * c_str() const noexcept
    {
        return getStorage<StringWithContext>().c_str;
    }

    const char ** context() const noexcept
    {
        return getStorage<StringWithContext>().context;
    }

    ExternalValueBase * external() const noexcept
    {
        return getStorage<ExternalValueBase *>();
    }

    const Bindings * attrs() const noexcept
    {
        return getStorage<Bindings *>();
    }

    const PrimOp * primOp() const noexcept
    {
        return getStorage<PrimOp *>();
    }

    bool boolean() const noexcept
    {
        return getStorage<bool>();
    }

    NixInt integer() const noexcept
    {
        return getStorage<NixInt>();
    }

    NixFloat fpoint() const noexcept
    {
        return getStorage<NixFloat>();
    }

    Lambda lambda() const noexcept
    {
        return getStorage<Lambda>();
    }

    ClosureThunk thunk() const noexcept
    {
        return getStorage<ClosureThunk>();
    }

    PrimOpApplicationThunk primOpApp() const noexcept
    {
        return getStorage<PrimOpApplicationThunk>();
    }

    FunctionApplicationThunk app() const noexcept
    {
        return getStorage<FunctionApplicationThunk>();
    }

    const char * pathStr() const noexcept
    {
        return getStorage<Path>().path;
    }

    SourceAccessor * pathAccessor() const noexcept
    {
        return getStorage<Path>().accessor;
    }
};

extern ExprBlackHole eBlackHole;

bool Value::isBlackhole() const
{
    return isThunk() && thunk().expr == (Expr *) &eBlackHole;
}

void Value::mkBlackhole()
{
    mkThunk(nullptr, (Expr *) &eBlackHole);
}

typedef std::vector<Value *, traceable_allocator<Value *>> ValueVector;
typedef std::unordered_map<
    Symbol,
    Value *,
    std::hash<Symbol>,
    std::equal_to<Symbol>,
    traceable_allocator<std::pair<const Symbol, Value *>>>
    ValueMap;
typedef std::map<Symbol, ValueVector, std::less<Symbol>, traceable_allocator<std::pair<const Symbol, ValueVector>>>
    ValueVectorMap;

/**
 * A value allocated in traceable memory.
 */
typedef std::shared_ptr<Value *> RootValue;

RootValue allocRootValue(Value * v);

void forceNoNullByte(std::string_view s, std::function<Pos()> = nullptr);
}
