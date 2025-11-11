#pragma once
///@file

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <memory_resource>
#include <span>
#include <string_view>
#include <type_traits>
#include <concepts>

#include "nix/expr/eval-gc.hh"
#include "nix/expr/value/context.hh"
#include "nix/util/source-path.hh"
#include "nix/expr/print-options.hh"
#include "nix/util/checked-arithmetic.hh"

#include <boost/unordered/unordered_flat_map_fwd.hpp>
#include <nlohmann/json_fwd.hpp>

namespace nix {

struct Value;
class BindingsBuilder;

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
    tUninitialized = 0,
    /* layout: Single/zero field payload */
    tInt = 1,
    tBool,
    tNull,
    tFloat,
    tExternal,
    tPrimOp,
    tAttrs,
    /* layout: Pair of pointers payload */
    tListSmall,
    tPrimOpApp,
    tApp,
    tThunk,
    tLambda,
    /* layout: Single untaggable field */
    tListN,
    tString,
    tPath,
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
class EvalMemory;
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
    ListBuilder(EvalMemory & mem, size_t size);

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

class StringData
{
public:
    using size_type = std::size_t;

    size_type size_;
    char data_[];

    /*
     * This in particular ensures that we cannot have a `StringData`
     * that we use by value, which is just what we want!
     *
     * Dynamically sized types aren't a thing in C++ and even flexible array
     * members are a language extension and beyond the realm of standard C++.
     * Technically, sizeof data_ member is 0 and the intended way to use flexible
     * array members is to allocate sizeof(StrindData) + count * sizeof(char) bytes
     * and the compiler will consider alignment restrictions for the FAM.
     *
     */

    StringData(StringData &&) = delete;
    StringData & operator=(StringData &&) = delete;
    StringData(const StringData &) = delete;
    StringData & operator=(const StringData &) = delete;
    ~StringData() = default;

private:
    StringData() = delete;

    explicit StringData(size_type size)
        : size_(size)
    {
    }

public:
    /**
     * Allocate StringData on the (possibly) GC-managed heap and copy
     * the contents of s to it.
     */
    static const StringData & make(std::string_view s);

    /**
     * Allocate StringData on the (possibly) GC-managed heap.
     * @param size Length of the string (without the NUL terminator).
     */
    static StringData & alloc(size_t size);

    size_t size() const
    {
        return size_;
    }

    char * data() noexcept
    {
        return data_;
    }

    const char * data() const noexcept
    {
        return data_;
    }

    const char * c_str() const noexcept
    {
        return data_;
    }

    constexpr std::string_view view() const noexcept
    {
        return std::string_view(data_, size_);
    }

    template<size_t N>
    struct Static;

    static StringData & make(std::pmr::memory_resource & resource, std::string_view s)
    {
        auto & res =
            *new (resource.allocate(sizeof(StringData) + s.size() + 1, alignof(StringData))) StringData(s.size());
        std::memcpy(res.data_, s.data(), s.size());
        res.data_[s.size()] = '\0';
        return res;
    }
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
        const StringData * str;

        /**
         * The type of the context itself.
         *
         * Currently, it is length-prefixed array of pointers to
         * null-terminated strings. The strings are specially formatted
         * to represent a flattening of the recursive sum type that is a
         * context element.
         *
         * @See NixStringContext for an more easily understood type,
         * that of the "builder" for this data structure.
         */
        struct Context
        {
            using value_type = const StringData *;
            using size_type = std::size_t;
            using iterator = const value_type *;

            Context(size_type size)
                : size_(size)
            {
            }

        private:
            /**
             * Number of items in the array
             */
            size_type size_;

            /**
             * @pre must be in sorted order
             */
            value_type elems[];

        public:
            iterator begin() const
            {
                return elems;
            }

            iterator end() const
            {
                return elems + size();
            }

            size_type size() const
            {
                return size_;
            }

            /**
             * @return null pointer when context.empty()
             */
            static Context * fromBuilder(const NixStringContext & context, EvalMemory & mem);
        };

        /**
         * May be null for a string without context.
         */
        const Context * context;
    };

    struct Path
    {
        SourceAccessor * accessor;
        const StringData * path;
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
inline constexpr bool useBitPackedValueStorage = (ptrSize == 8) && (__STDCPP_DEFAULT_NEW_ALIGNMENT__ >= 16);

} // namespace detail

/**
 * Value storage that is optimized for 64 bit systems.
 * Packs discriminator bits into the pointer alignment niches.
 */
template<std::size_t ptrSize>
class alignas(16) ValueStorage<ptrSize, std::enable_if_t<detail::useBitPackedValueStorage<ptrSize>>>
    : public detail::ValueBase
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
    using Payload = std::array<PackedPointer, 2>;
    Payload payload = {};

    static constexpr int discriminatorBits = 3;
    static constexpr PackedPointer discriminatorMask = (PackedPointer(1) << discriminatorBits) - 1;

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
    enum PrimaryDiscriminator : int {
        pdUninitialized = 0,
        pdSingleDWord, //< layout: Single/zero field payload
        /* The order of these enumations must be the same as in InternalType. */
        pdListN, //< layout: Single untaggable field.
        pdString,
        pdPath,
        pdPairOfPointers, //< layout: Pair of pointers payload
    };

    template<typename T>
        requires std::is_pointer_v<T>
    static T untagPointer(PackedPointer val) noexcept
    {
        return std::bit_cast<T>(val & ~discriminatorMask);
    }

    PrimaryDiscriminator getPrimaryDiscriminator() const noexcept
    {
        return static_cast<PrimaryDiscriminator>(payload[0] & discriminatorMask);
    }

    static void assertAligned(PackedPointer val) noexcept
    {
        assert((val & discriminatorMask) == 0 && "Pointer is not 8 bytes aligned");
    }

    template<InternalType type>
    void setSingleDWordPayload(PackedPointer untaggedVal) noexcept
    {
        /* There's plenty of free upper bits in the first dword, which is
           used only for the discriminator. */
        payload[0] = static_cast<int>(pdSingleDWord) | (static_cast<int>(type) << discriminatorBits);
        payload[1] = untaggedVal;
    }

    template<PrimaryDiscriminator discriminator, typename T, typename U>
    void setUntaggablePayload(T * firstPtrField, U untaggableField) noexcept
    {
        static_assert(discriminator >= pdListN && discriminator <= pdPath);
        auto firstFieldPayload = std::bit_cast<PackedPointer>(firstPtrField);
        assertAligned(firstFieldPayload);
        payload[0] = static_cast<int>(discriminator) | firstFieldPayload;
        payload[1] = std::bit_cast<PackedPointer>(untaggableField);
    }

    template<InternalType type, typename T, typename U>
    void setPairOfPointersPayload(T * firstPtrField, U * secondPtrField) noexcept
    {
        static_assert(type >= tListSmall && type <= tLambda);
        {
            auto firstFieldPayload = std::bit_cast<PackedPointer>(firstPtrField);
            assertAligned(firstFieldPayload);
            payload[0] = static_cast<int>(pdPairOfPointers) | firstFieldPayload;
        }
        {
            auto secondFieldPayload = std::bit_cast<PackedPointer>(secondPtrField);
            assertAligned(secondFieldPayload);
            payload[1] = (type - tListSmall) | secondFieldPayload;
        }
    }

    template<typename T, typename U>
        requires std::is_pointer_v<T> && std::is_pointer_v<U>
    void getPairOfPointersPayload(T & firstPtrField, U & secondPtrField) const noexcept
    {
        firstPtrField = untagPointer<T>(payload[0]);
        secondPtrField = untagPointer<U>(payload[1]);
    }

protected:
    /** Get internal type currently occupying the storage. */
    InternalType getInternalType() const noexcept
    {
        switch (auto pd = getPrimaryDiscriminator()) {
        case pdUninitialized:
            /* Discriminator value of zero is used to distinguish uninitialized values. */
            return tUninitialized;
        case pdSingleDWord:
            /* Payloads that only use up a single double word store the InternalType
               in the upper bits of the first double word. */
            return InternalType(payload[0] >> discriminatorBits);
        /* The order must match that of the enumerations defined in InternalType. */
        case pdListN:
        case pdString:
        case pdPath:
            return static_cast<InternalType>(tListN + (pd - pdListN));
        case pdPairOfPointers:
            return static_cast<InternalType>(tListSmall + (payload[1] & discriminatorMask));
        [[unlikely]] default:
            unreachable();
        }
    }

#define NIX_VALUE_STORAGE_DEF_PAIR_OF_PTRS(TYPE, MEMBER_A, MEMBER_B)                                   \
                                                                                                       \
    void getStorage(TYPE & val) const noexcept                                                         \
    {                                                                                                  \
        getPairOfPointersPayload(val MEMBER_A, val MEMBER_B);                                          \
    }                                                                                                  \
                                                                                                       \
    void setStorage(TYPE val) noexcept                                                                 \
    {                                                                                                  \
        setPairOfPointersPayload<detail::payloadTypeToInternalType<TYPE>>(val MEMBER_A, val MEMBER_B); \
    }

    NIX_VALUE_STORAGE_DEF_PAIR_OF_PTRS(SmallList, [0], [1])
    NIX_VALUE_STORAGE_DEF_PAIR_OF_PTRS(PrimOpApplicationThunk, .left, .right)
    NIX_VALUE_STORAGE_DEF_PAIR_OF_PTRS(FunctionApplicationThunk, .left, .right)
    NIX_VALUE_STORAGE_DEF_PAIR_OF_PTRS(ClosureThunk, .env, .expr)
    NIX_VALUE_STORAGE_DEF_PAIR_OF_PTRS(Lambda, .env, .fun)

#undef NIX_VALUE_STORAGE_DEF_PAIR_OF_PTRS

    void getStorage(NixInt & integer) const noexcept
    {
        /* PackedPointerType -> int64_t here is well-formed, since the standard requires
           this conversion to follow 2's complement rules. This is just a no-op. */
        integer = NixInt(payload[1]);
    }

    void getStorage(bool & boolean) const noexcept
    {
        boolean = payload[1];
    }

    void getStorage(Null & null) const noexcept {}

    void getStorage(NixFloat & fpoint) const noexcept
    {
        fpoint = std::bit_cast<NixFloat>(payload[1]);
    }

    void getStorage(ExternalValueBase *& external) const noexcept
    {
        external = std::bit_cast<ExternalValueBase *>(payload[1]);
    }

    void getStorage(PrimOp *& primOp) const noexcept
    {
        primOp = std::bit_cast<PrimOp *>(payload[1]);
    }

    void getStorage(Bindings *& attrs) const noexcept
    {
        attrs = std::bit_cast<Bindings *>(payload[1]);
    }

    void getStorage(List & list) const noexcept
    {
        list.elems = untagPointer<decltype(list.elems)>(payload[0]);
        list.size = payload[1];
    }

    void getStorage(StringWithContext & string) const noexcept
    {
        string.context = untagPointer<decltype(string.context)>(payload[0]);
        string.str = std::bit_cast<const StringData *>(payload[1]);
    }

    void getStorage(Path & path) const noexcept
    {
        path.accessor = untagPointer<decltype(path.accessor)>(payload[0]);
        path.path = std::bit_cast<const StringData *>(payload[1]);
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
        setUntaggablePayload<pdString>(string.context, string.str);
    }

    void setStorage(Path path) noexcept
    {
        setUntaggablePayload<pdPath>(path.accessor, path.path);
    }
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

    /**
     * Empty list constant.
     *
     * This is _not_ a singleton. Pointer equality is _not_ sufficient.
     */
    static Value vEmptyList;

    /**
     * `null` constant.
     *
     * This is _not_ a singleton. Pointer equality is _not_ sufficient.
     */
    static Value vNull;

    /**
     * `true` constant.
     *
     * This is _not_ a singleton. Pointer equality is _not_ sufficient.
     */
    static Value vTrue;

    /**
     * `true` constant.
     *
     * This is _not_ a singleton. Pointer equality is _not_ sufficient.
     */
    static Value vFalse;

private:
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

    void mkStringNoCopy(const StringData & s, const Value::StringWithContext::Context * context = nullptr) noexcept
    {
        setStorage(StringWithContext{.str = &s, .context = context});
    }

    void mkString(std::string_view s);

    void mkString(std::string_view s, const NixStringContext & context, EvalMemory & mem);

    void mkStringMove(const StringData & s, const NixStringContext & context, EvalMemory & mem);

    void mkPath(const SourcePath & path);

    inline void mkPath(SourceAccessor * accessor, const StringData & path) noexcept
    {
        setStorage(Path{.accessor = accessor, .path = &path});
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
        switch (builder.size) {
        case 0:
            setStorage(List{.size = 0, .elems = nullptr});
            break;
        case 1:
            setStorage(std::array<Value *, 2>{builder.inlineElems[0], nullptr});
            break;
        case 2:
            setStorage(std::array<Value *, 2>{builder.inlineElems[0], builder.inlineElems[1]});
            break;
        default:
            setStorage(List{.size = builder.size, .elems = builder.elems});
            break;
        }
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
     * computation. In particular, function applications are
     * non-trivial.
     */
    bool isTrivial() const;

    SourcePath path() const
    {
        return SourcePath(
            ref(pathAccessor()->shared_from_this()), CanonPath(CanonPath::unchecked_t(), std::string(pathStrView())));
    }

    const StringData & string_data() const noexcept
    {
        return *getStorage<StringWithContext>().str;
    }

    const char * c_str() const noexcept
    {
        return getStorage<StringWithContext>().str->data();
    }

    std::string_view string_view() const noexcept
    {
        return string_data().view();
    }

    const Value::StringWithContext::Context * context() const noexcept
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
        return getStorage<Path>().path->c_str();
    }

    std::string_view pathStrView() const noexcept
    {
        return getStorage<Path>().path->view();
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
typedef boost::unordered_flat_map<
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
