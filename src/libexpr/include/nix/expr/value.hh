#pragma once
///@file

#include <atomic>
#include <cassert>
#include <span>
#include <type_traits>
#include <concepts>

#include "nix/expr/eval-gc.hh"
#include "nix/expr/value/context.hh"
#include "nix/util/source-path.hh"
#include "nix/expr/print-options.hh"
#include "nix/util/checked-arithmetic.hh"

#include <nlohmann/json_fwd.hpp>

namespace nix {

struct Value;
class BindingsBuilder;

static constexpr int discriminatorBits = 3;

enum PrimaryDiscriminator : int {
    pdSingleDWord = 0,
    pdThunk = 1,
    pdPending = 2,
    pdAwaited = 3,
    pdPairOfPointers = 4,
    pdListN = 5, // FIXME: get rid of this by putting the size in the first word
    pdString = 6,
    pdPath = 7, // FIXME: get rid of this by ditching the `accessor` field
};

/**
 * Internal type discriminator, which is more detailed than `ValueType`, as
 * it specifies the exact representation used (for types that have multiple
 * possible representations).
 *
 * @warning The ordering is very significant. See ValueStorage::getInternalType() for details
 * about how this is mapped into the alignment bits to save significant memory.
 * This also restricts the number of internal types represented with distinct memory layouts.
 */
typedef enum {
    /* Values that have more type bits in the first word, and the
       payload (a single word) in the second word. */
    tUninitialized = PrimaryDiscriminator::pdSingleDWord | (0 << discriminatorBits),
    tInt = PrimaryDiscriminator::pdSingleDWord | (1 << discriminatorBits),
    tFloat = PrimaryDiscriminator::pdSingleDWord | (2 << discriminatorBits),
    tBool = PrimaryDiscriminator::pdSingleDWord | (3 << discriminatorBits),
    tNull = PrimaryDiscriminator::pdSingleDWord | (4 << discriminatorBits),
    tAttrs = PrimaryDiscriminator::pdSingleDWord | (5 << discriminatorBits),
    tPrimOp = PrimaryDiscriminator::pdSingleDWord | (6 << discriminatorBits),
    tFailed = PrimaryDiscriminator::pdSingleDWord | (7 << discriminatorBits),
    tExternal = PrimaryDiscriminator::pdSingleDWord | (8 << discriminatorBits),

    /* Thunks. */
    tThunk = PrimaryDiscriminator::pdThunk | (0 << discriminatorBits),
    tApp = PrimaryDiscriminator::pdThunk | (1 << discriminatorBits),

    tPending = PrimaryDiscriminator::pdPending,
    tAwaited = PrimaryDiscriminator::pdAwaited,

    /* Values that consist of two pointers. The second word contains
       more type bits in its alignment niche. */
    tListSmall = PrimaryDiscriminator::pdPairOfPointers | (0 << discriminatorBits),
    tPrimOpApp = PrimaryDiscriminator::pdPairOfPointers | (1 << discriminatorBits),
    tLambda = PrimaryDiscriminator::pdPairOfPointers | (2 << discriminatorBits),

    /* Special values. */
    tListN = PrimaryDiscriminator::pdListN,
    tString = PrimaryDiscriminator::pdString,
    tPath = PrimaryDiscriminator::pdPath,
} InternalType;

/**
 * Return true if `type` denotes a "finished" value, i.e. a weak-head
 * normal form.
 *
 * Note that tPrimOpApp is considered "finished" because it represents
 * a primop call with an incomplete number of arguments, and therefore
 * cannot be evaluated further.
 */
inline bool isFinished(InternalType t)
{
    return t != tUninitialized && t != tThunk && t != tApp && t != tPending && t != tAwaited;
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
    nExternal,
} ValueType;

class Bindings;
struct Env;
struct Expr;
struct ExprLambda;
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

    struct Failed
    {
        std::exception_ptr ex;
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
    MACRO(ValueBase::Failed *, failed, tFailed)                     \
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

} // namespace detail

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
template<std::size_t ptrSize, typename Enable = void>
class ValueStorage : public detail::ValueBase
{
protected:
    using Payload = union
    {
#define NIX_VALUE_STORAGE_DEFINE_FIELD(T, FIELD_NAME, DISCRIMINATOR) T FIELD_NAME;
        NIX_VALUE_STORAGE_FOR_EACH_FIELD(NIX_VALUE_STORAGE_DEFINE_FIELD)
#undef NIX_VALUE_STORAGE_DEFINE_FIELD
    };

private:
    InternalType internalType = tUninitialized;
    Payload payload;

protected:
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

namespace detail {

/* Whether to use a specialization of ValueStorage that does bitpacking into
   alignment niches. */
template<std::size_t ptrSize>
inline constexpr bool useBitPackedValueStorage = (ptrSize == 8) && (__STDCPP_DEFAULT_NEW_ALIGNMENT__ >= 8);

} // namespace detail

/**
 * Value storage that is optimized for 64 bit systems.
 * Packs discriminator bits into the pointer alignment niches.
 */
template<std::size_t ptrSize>
class ValueStorage<ptrSize, std::enable_if_t<detail::useBitPackedValueStorage<ptrSize>>> : public detail::ValueBase
{
    /* Needs a dependent type name in order for member functions (and
     * potentially ill-formed bit casts) to be SFINAE'd out.
     *
     * Otherwise some member functions could possibly be instantiated for 32 bit
     * systems and fail due to an unsatisfied constraint.
     */
    template<std::size_t size>
    struct PackedPointerTypeStruct
    {
        using type = std::uint64_t;
    };

    using PackedPointer = typename PackedPointerTypeStruct<ptrSize>::type;

    /**
     * For multithreaded evaluation, we have to make sure that thunks/apps
     * (the only mutable types of values) are updated in a safe way. A
     * value can have the following states (see `force()`):
     *
     * * "thunk"/"app". When forced, this value transitions to
     *   "pending". The current thread will evaluate the
     *   thunk/app. When done, it will override the value with the
     *   result. If the value is at that point in the "awaited" state,
     *   the thread will wake up any waiting threads.
     *
     * * "pending". This means it's currently being evaluated. If
     *   another thread forces this value, it transitions to "awaited"
     *   and the thread will wait for the value to be updated (see
     *   `waitOnThunk()`).
     *
     * * "awaited". Like pending, only it means that there already are
     *   one or more threads waiting for this thunk.
     *
     * To ensure race-free access, the non-atomic word `p1` must
     * always be updated before `p0`. Writes to `p0` should use
     * *release* semantics (so that `p1` and any referenced values become
     * visible to threads that read `p0`), and reads from `p0` should
     * use `*acquire* semantics.
     *
     * Note: at some point, we may want to switch to 128-bit atomics
     * so that `p0` and `p1` can be updated together
     * atomically. However, 128-bit atomics are a bit problematic at
     * present on x86_64 (see
     * e.g. https://ibraheem.ca/posts/128-bit-atomics/).
     */
    std::atomic<PackedPointer> p0{0};
    PackedPointer p1{0};

    static constexpr PackedPointer discriminatorMask = (PackedPointer(1) << discriminatorBits) - 1;

    // FIXME: move/update
    /**
     * The value is stored as a pair of 8-byte double words. All pointers are assumed
     * to be 8-byte aligned. This gives us at most 6 bits of discriminator bits
     * of free storage. In some cases when one double word can't be tagged the whole
     * discriminator is stored in the first double word.
     *
     * The layout of discriminator bits is determined by the 3 bits of PrimaryDiscriminator,
     * which are always stored in the lower 3 bits of the first dword of the payload.
     * The memory layout has 3 types depending on the PrimaryDiscriminator value.
     *
     * PrimaryDiscriminator::pdSingleDWord - Only the second dword carries the data.
     * That leaves the first 8 bytes free for storing the InternalType in the upper
     * bits.
     *
     * PrimaryDiscriminator::pdListN - pdPath - Only has 3 available padding bits
     * because:
     * - tListN needs a size, whose lower bits we can't borrow.
     * - tString and tPath have C-string fields, which don't necessarily need to
     * be aligned.
     *
     * In this case we reserve their discriminators directly in the PrimaryDiscriminator
     * bits stored in payload[0].
     *
     * PrimaryDiscriminator::pdPairOfPointers - Payloads that consist of a pair of pointers.
     * In this case the 3 lower bits of payload[1] can be tagged.
     *
     * The primary discriminator with value 0 is reserved for uninitialized Values,
     * which are useful for diagnostics in C bindings.
     */

    template<typename T>
        requires std::is_pointer_v<T>
    static T untagPointer(PackedPointer val) noexcept
    {
        return std::bit_cast<T>(val & ~discriminatorMask);
    }

    PrimaryDiscriminator getPrimaryDiscriminator() const noexcept
    {
        return static_cast<PrimaryDiscriminator>(p0 & discriminatorMask);
    }

    static void assertAligned(PackedPointer val) noexcept
    {
        assert((val & discriminatorMask) == 0 && "Pointer is not 8 bytes aligned");
    }

    void finish(PackedPointer p0_, PackedPointer p1_)
    {
        debug("FINISH %x %08x %08x", this, p0_, p1_);

        // Note: p1 *must* be updated before p0.
        p1 = p1_;
        p0_ = p0.exchange(p0_, std::memory_order_release);

        auto pd = static_cast<PrimaryDiscriminator>(p0_ & discriminatorMask);
        if (pd == pdPending)
            // Nothing to do; no thread is waiting on this thunk.
            ;
        else if (pd == pdAwaited)
            // Slow path: wake up the threads that are waiting on this
            // thunk.
            notifyWaiters();
        else if (pd == pdThunk) {
            printError("BAD FINISH %x", this);
            unreachable();
        }
    }

    template<InternalType type>
    void setSingleDWordPayload(PackedPointer untaggedVal) noexcept
    {
        /* There's plenty of free upper bits in the first byte, which
           is used only for the discriminator. */
        finish(static_cast<uint8_t>(type), untaggedVal);
    }

    template<PrimaryDiscriminator discriminator, typename T, typename U>
    void setUntaggablePayload(T * firstPtrField, U untaggableField) noexcept
    {
        static_assert(discriminator >= pdListN && discriminator <= pdPath);
        auto firstFieldPayload = std::bit_cast<PackedPointer>(firstPtrField);
        assertAligned(firstFieldPayload);
        finish(static_cast<int>(discriminator) | firstFieldPayload, std::bit_cast<PackedPointer>(untaggableField));
    }

    template<InternalType type, typename T, typename U>
    void setPairOfPointersPayload(T * firstPtrField, U * secondPtrField) noexcept
    {
        static_assert(type >= tListSmall && type <= tLambda);
        auto firstFieldPayload = std::bit_cast<PackedPointer>(firstPtrField);
        assertAligned(firstFieldPayload);
        auto secondFieldPayload = std::bit_cast<PackedPointer>(secondPtrField);
        assertAligned(secondFieldPayload);
        finish(
            static_cast<int>(pdPairOfPointers) | firstFieldPayload,
            ((type - tListSmall) >> discriminatorBits) | secondFieldPayload);
    }

    template<InternalType type, typename T, typename U>
    void setThunkPayload(T * firstPtrField, U * secondPtrField) noexcept
    {
        static_assert(type >= tThunk && type <= tApp);
        auto secondFieldPayload = std::bit_cast<PackedPointer>(secondPtrField);
        assertAligned(secondFieldPayload);
        p1 = ((type - tThunk) >> discriminatorBits) | secondFieldPayload;
        auto firstFieldPayload = std::bit_cast<PackedPointer>(firstPtrField);
        assertAligned(firstFieldPayload);
        // Note: awaited values can never become a thunk, so no need
        // to check for waiters.
        p0.store(static_cast<int>(pdThunk) | firstFieldPayload, std::memory_order_relaxed);
    }

    template<typename T, typename U>
        requires std::is_pointer_v<T> && std::is_pointer_v<U>
    void getPairOfPointersPayload(T & firstPtrField, U & secondPtrField) const noexcept
    {
        firstPtrField = untagPointer<T>(p0);
        secondPtrField = untagPointer<U>(p1);
    }

protected:
    /** Get internal type currently occupying the storage. */
    InternalType getInternalType() const noexcept
    {
        switch (auto pd = getPrimaryDiscriminator()) {
        case pdSingleDWord:
            /* Payloads that only use up a single double word store
               the full InternalType in the first byte. */
            return InternalType(p0 & 0xff);
        case pdThunk:
            return static_cast<InternalType>(tThunk + ((p1 & discriminatorMask) << discriminatorBits));
        case pdPending:
            return tPending;
        case pdAwaited:
            return tAwaited;
        case pdPairOfPointers:
            return static_cast<InternalType>(tListSmall + ((p1 & discriminatorMask) << discriminatorBits));
        /* The order must match that of the enumerations defined in InternalType. */
        case pdListN:
        case pdString:
        case pdPath:
            return static_cast<InternalType>(tListN + (pd - pdListN));
        [[unlikely]] default:
            unreachable();
        }
    }

#define NIX_VALUE_STORAGE_DEF_PAIR_OF_PTRS(TYPE, SET, MEMBER_A, MEMBER_B)         \
                                                                                  \
    void getStorage(TYPE & val) const noexcept                                    \
    {                                                                             \
        getPairOfPointersPayload(val MEMBER_A, val MEMBER_B);                     \
    }                                                                             \
                                                                                  \
    void setStorage(TYPE val) noexcept                                            \
    {                                                                             \
        SET<detail::payloadTypeToInternalType<TYPE>>(val MEMBER_A, val MEMBER_B); \
    }

    NIX_VALUE_STORAGE_DEF_PAIR_OF_PTRS(SmallList, setPairOfPointersPayload, [0], [1])
    NIX_VALUE_STORAGE_DEF_PAIR_OF_PTRS(PrimOpApplicationThunk, setPairOfPointersPayload, .left, .right)
    NIX_VALUE_STORAGE_DEF_PAIR_OF_PTRS(Lambda, setPairOfPointersPayload, .env, .fun)
    NIX_VALUE_STORAGE_DEF_PAIR_OF_PTRS(FunctionApplicationThunk, setThunkPayload, .left, .right)
    NIX_VALUE_STORAGE_DEF_PAIR_OF_PTRS(ClosureThunk, setThunkPayload, .env, .expr)

#undef NIX_VALUE_STORAGE_DEF_PAIR_OF_PTRS

    void getStorage(NixInt & integer) const noexcept
    {
        /* PackedPointerType -> int64_t here is well-formed, since the standard requires
           this conversion to follow 2's complement rules. This is just a no-op. */
        integer = NixInt(p1);
    }

    void getStorage(bool & boolean) const noexcept
    {
        boolean = p1;
    }

    void getStorage(Null & null) const noexcept {}

    void getStorage(NixFloat & fpoint) const noexcept
    {
        fpoint = std::bit_cast<NixFloat>(p1);
    }

    void getStorage(ExternalValueBase *& external) const noexcept
    {
        external = std::bit_cast<ExternalValueBase *>(p1);
    }

    void getStorage(PrimOp *& primOp) const noexcept
    {
        primOp = std::bit_cast<PrimOp *>(p1);
    }

    void getStorage(Bindings *& attrs) const noexcept
    {
        attrs = std::bit_cast<Bindings *>(p1);
    }

    void getStorage(List & list) const noexcept
    {
        list.elems = untagPointer<decltype(list.elems)>(p0);
        list.size = p1;
    }

    void getStorage(StringWithContext & string) const noexcept
    {
        string.context = untagPointer<decltype(string.context)>(p0);
        string.c_str = std::bit_cast<const char *>(p1);
    }

    void getStorage(Path & path) const noexcept
    {
        path.accessor = untagPointer<decltype(path.accessor)>(p0);
        path.path = std::bit_cast<const char *>(p1);
    }

    void getStorage(Failed *& failed) const noexcept
    {
        failed = std::bit_cast<Failed *>(p1);
    }

    void setStorage(NixInt integer) noexcept
    {
        setSingleDWordPayload<tInt>(integer.value);
    }

    void setStorage(bool boolean) noexcept
    {
        setSingleDWordPayload<tBool>(boolean);
    }

    void setStorage(Null path) noexcept
    {
        setSingleDWordPayload<tNull>(0);
    }

    void setStorage(NixFloat fpoint) noexcept
    {
        setSingleDWordPayload<tFloat>(std::bit_cast<PackedPointer>(fpoint));
    }

    void setStorage(ExternalValueBase * external) noexcept
    {
        setSingleDWordPayload<tExternal>(std::bit_cast<PackedPointer>(external));
    }

    void setStorage(PrimOp * primOp) noexcept
    {
        setSingleDWordPayload<tPrimOp>(std::bit_cast<PackedPointer>(primOp));
    }

    void setStorage(Bindings * bindings) noexcept
    {
        setSingleDWordPayload<tAttrs>(std::bit_cast<PackedPointer>(bindings));
    }

    void setStorage(List list) noexcept
    {
        setUntaggablePayload<pdListN>(list.elems, list.size);
    }

    void setStorage(StringWithContext string) noexcept
    {
        setUntaggablePayload<pdString>(string.context, string.c_str);
    }

    void setStorage(Path path) noexcept
    {
        setUntaggablePayload<pdPath>(path.accessor, path.path);
    }

    void setStorage(Failed * failed) noexcept
    {
        setSingleDWordPayload<tFailed>(std::bit_cast<PackedPointer>(failed));
    }

    ValueStorage() {}

    ValueStorage(const ValueStorage & v)
    {
        *this = v;
    }

    /**
     * Copy a value. This is not allowed to be a thunk to avoid
     * accidental work duplication.
     */
    ValueStorage & operator=(const ValueStorage & v)
    {
        auto p1_ = v.p1;
        auto p0_ = v.p0.load(std::memory_order_acquire);
        auto pd = static_cast<PrimaryDiscriminator>(p0_ & discriminatorMask);
        if (pd == pdThunk || pd == pdPending || pd == pdAwaited) {
            printError("UNFINISHED %x %08x %08x", this, p0_, p1_);
            unreachable();
        }
        finish(p0_, p1_);
        return *this;
    }

public:

    inline void reset()
    {
        p1 = 0;
        p0.store(0, std::memory_order_relaxed);
    }

    /// Only used for testing.
    inline void mkBlackhole()
    {
        p0.store(pdPending, std::memory_order_relaxed);
    }

    void force(EvalState & state, PosIdx pos);

private:

    /**
     * Given a thunk that was observed to be in the pending or awaited
     * state, wait for it to finish. Returns the first word of the
     * value.
     */
    PackedPointer waitOnThunk(EvalState & state, bool awaited);

    /**
     * Wake up any threads that are waiting on this value.
     */
    void notifyWaiters();
};

/**
 * View into a list of Value * that is itself immutable.
 *
 * Since not all representations of ValueStorage can provide
 * a pointer to a const array of Value * this proxy class either
 * stores the small list inline or points to the big list.
 */
class ListView
{
    using SpanType = std::span<Value * const>;
    using SmallList = detail::ValueBase::SmallList;
    using List = detail::ValueBase::List;

    std::variant<SmallList, List> raw;

public:
    ListView(SmallList list)
        : raw(list)
    {
    }

    ListView(List list)
        : raw(list)
    {
    }

    Value * const * data() const & noexcept
    {
        return std::visit(
            overloaded{
                [](const SmallList & list) { return list.data(); }, [](const List & list) { return list.elems; }},
            raw);
    }

    std::size_t size() const noexcept
    {
        return std::visit(
            overloaded{
                [](const SmallList & list) -> std::size_t { return list.back() == nullptr ? 1 : 2; },
                [](const List & list) -> std::size_t { return list.size; }},
            raw);
    }

    Value * operator[](std::size_t i) const noexcept
    {
        return data()[i];
    }

    SpanType span() const &
    {
        return SpanType(data(), size());
    }

    /* Ensure that no dangling views can be created accidentally, as that
       would lead to hard to diagnose bugs that only affect small lists. */
    SpanType span() && = delete;
    Value * const * data() && noexcept = delete;

    /**
     * Random-access iterator that only allows iterating over a constant range
     * of mutable Value pointers.
     *
     * @note Not a pointer to minimize potential misuses and implicitly relying
     * on the iterator being a pointer.
     **/
    class iterator
    {
    public:
        using value_type = Value *;
        using pointer = const value_type *;
        using reference = const value_type &;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::random_access_iterator_tag;

    private:
        pointer ptr = nullptr;

        friend class ListView;

        iterator(pointer ptr)
            : ptr(ptr)
        {
        }

    public:
        iterator() = default;

        reference operator*() const
        {
            return *ptr;
        }

        const value_type * operator->() const
        {
            return ptr;
        }

        reference operator[](difference_type diff) const
        {
            return ptr[diff];
        }

        iterator & operator++()
        {
            ++ptr;
            return *this;
        }

        iterator operator++(int)
        {
            pointer tmp = ptr;
            ++*this;
            return iterator(tmp);
        }

        iterator & operator--()
        {
            --ptr;
            return *this;
        }

        iterator operator--(int)
        {
            pointer tmp = ptr;
            --*this;
            return iterator(tmp);
        }

        iterator & operator+=(difference_type diff)
        {
            ptr += diff;
            return *this;
        }

        iterator operator+(difference_type diff) const
        {
            return iterator(ptr + diff);
        }

        friend iterator operator+(difference_type diff, const iterator & rhs)
        {
            return iterator(diff + rhs.ptr);
        }

        iterator & operator-=(difference_type diff)
        {
            ptr -= diff;
            return *this;
        }

        iterator operator-(difference_type diff) const
        {
            return iterator(ptr - diff);
        }

        difference_type operator-(const iterator & rhs) const
        {
            return ptr - rhs.ptr;
        }

        std::strong_ordering operator<=>(const iterator & rhs) const = default;
    };

    using const_iterator = iterator;

    iterator begin() const &
    {
        return data();
    }

    iterator end() const &
    {
        return data() + size();
    }

    /* Ensure that no dangling iterators can be created accidentally, as that
       would lead to hard to diagnose bugs that only affect small lists. */
    iterator begin() && = delete;
    iterator end() && = delete;
};

static_assert(std::random_access_iterator<ListView::iterator>);

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

    // FIXME: optimize, only look at first word
    inline bool isFinished() const
    {
        return nix::isFinished(getInternalType());
    }

    // Functions needed to distinguish the type
    // These should be removed eventually, by putting the functionality that's
    // needed by callers into methods of this type

    inline bool isThunk() const
    {
        return isa<tThunk>();
    }

    inline bool isApp() const
    {
        return isa<tApp>();
    }

    inline bool isBlackhole() const
    {
        auto t = getInternalType();
        return t == tPending || t == tAwaited;
    }

    // type() == nFunction
    inline bool isLambda() const
    {
        return isa<tLambda>();
    }

    inline bool isPrimOp() const
    {
        return isa<tPrimOp>();
    }

    inline bool isPrimOpApp() const
    {
        return isa<tPrimOpApp>();
    }

    inline bool isFailed() const
    {
        return isa<tFailed>();
    }

    /**
     * Returns the normal type of a Value. This only returns nThunk if
     * the Value hasn't been forceValue'd
     */
    inline ValueType type() const
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
        case tFailed:
            return nFailed;
        case tThunk:
        case tApp:
        case tPending:
        case tAwaited:
            return nThunk;
        }
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

    inline void mkFailed() noexcept
    {
        setStorage(new Value::Failed{.ex = std::current_exception()});
    }

    bool isList() const noexcept
    {
        return isa<tListSmall, tListN>();
    }

    ListView listView() const noexcept
    {
        return isa<tListSmall>() ? ListView(getStorage<SmallList>()) : ListView(getStorage<List>());
    }

    size_t listSize() const noexcept
    {
        return isa<tListSmall>() ? (getStorage<SmallList>()[1] == nullptr ? 1 : 2) : getStorage<List>().size;
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

    // FIXME: remove this since reading it is racy.
    ClosureThunk thunk() const noexcept
    {
        return getStorage<ClosureThunk>();
    }

    PrimOpApplicationThunk primOpApp() const noexcept
    {
        return getStorage<PrimOpApplicationThunk>();
    }

    // FIXME: remove this since reading it is racy.
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

    Failed * failed() const noexcept
    {
        return getStorage<Failed *>();
    }
};

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
} // namespace nix
