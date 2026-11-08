#pragma once
///@file

#include <memory_resource>
#include "nix/expr/value.hh"
#include "nix/expr/static-string-data.hh"
#include "nix/util/chunked-vector.hh"
#include "nix/util/bump-memory-resource.hh"
#include "nix/util/error.hh"

#include <boost/version.hpp>
#include <boost/unordered/concurrent_flat_set.hpp>

namespace nix {

class StaticSymbolTable;

/**
 * Symbols have the property that they can be compared efficiently
 * (using an equality test), because the symbol table stores only one
 * copy of each string.
 */
class Symbol
{
    friend class SymbolTable;
    friend class SymbolStr;
    friend class StaticSymbolTable;
    friend struct Value;

public:
    using value_type = uint32_t;

private:
    value_type id;

    static_assert(sizeof(detail::ValueBase::Symbol::symbol) >= sizeof(id));

    explicit constexpr Symbol(value_type id) noexcept
        : id(id)
    {
    }

public:
    constexpr Symbol() noexcept
        : id(0)
    {
    }

    [[gnu::always_inline]]
    constexpr explicit operator bool() const noexcept
    {
        return id > 0;
    }

    /**
     * The ID is a private implementation detail that should generally not be observed. However, we expose here just for
     * sake of `switch...case`, which needs to dispatch on numbers. */
    [[gnu::always_inline]]
    constexpr value_type getId() const noexcept
    {
        return id;
    }

    constexpr auto operator<=>(const Symbol & other) const noexcept = default;

    friend class std::hash<Symbol>;
};

inline Symbol Value::symbol() const noexcept
{
    if (!isa<tSymbol>())
        return {};
    detail::ValueBase::Symbol payload;
    ValueStorage::getStorage(payload);
    return nix::Symbol(static_cast<nix::Symbol::value_type>(payload.symbol));
}

/**
 * This class mainly exists to give us an operator<< for ostreams. We could also
 * return plain strings from SymbolTable, but then we'd have to wrap every
 * instance of a symbol that is fmt()ed, which is inconvenient and error-prone.
 */
class SymbolStr
{
    friend class SymbolTable;

    constexpr static size_t chunkSize{131072};
    /**
     * Each symbol is stored as a string `Value` of internal type `tSymbol`,
     * which carries the symbol id. `Value::toPtr` hands out pointers to these
     * values, so that attribute names can be returned without allocation.
     */
    using SymbolValueStore = ChunkedVector<
        Value,
        chunkSize,
        /*MaxChunks=*/std::numeric_limits<Symbol::value_type>::max() / chunkSize>;

    const Value * s;

    struct Key
    {
        using HashType = boost::hash<std::string_view>;

        SymbolValueStore & store;
        std::string_view s;
        std::size_t hash;
        std::pmr::memory_resource & resource;

        Key(SymbolValueStore & store, std::string_view s, std::pmr::memory_resource & stringMemory)
            : store(store)
            , s(s)
            , hash(HashType{}(s))
            , resource(stringMemory)
        {
        }
    };

public:
    SymbolStr(const Value & s) noexcept
        : s(&s)
    {
    }

    SymbolStr(const Key & key)
    {
        const auto & [v, idx] = key.store.add(Value{});
        detail::ValueBase::Symbol symbol;
        symbol.symbol = idx + 1;
        if (key.s.empty()) {
            symbol.str = &""_sds;
        } else {
            symbol.str = &StringData::make(key.resource, key.s);
        }
        v.setStorage(symbol);
        this->s = &v;
    }

    bool operator==(std::string_view s2) const noexcept
    {
        return s->string_view() == s2;
    }

    [[gnu::always_inline]]
    const StringData & string_data() const noexcept
    {
        return s->string_data();
    }

    [[gnu::always_inline]]
    const char * c_str() const noexcept
    {
        return s->c_str();
    }

    [[gnu::always_inline]] operator std::string_view() const noexcept
    {
        return s->string_view();
    }

    friend std::ostream & operator<<(std::ostream & os, const SymbolStr & symbol);

    [[gnu::always_inline]]
    bool empty() const noexcept
    {
        auto * p = &s->string_data();
        // Save a dereference in the sentinel value case
        if (p == &""_sds)
            return true;
        return p->size() == 0;
    }

    [[gnu::always_inline]]
    size_t size() const noexcept
    {
        return s->string_data().size();
    }

    [[gnu::always_inline]]
    const Value * valuePtr() const noexcept
    {
        return s;
    }

    explicit operator Symbol() const noexcept
    {
        return s->symbol();
    }

    struct Hash
    {
        using is_transparent = void;
        using is_avalanching = std::true_type;

        std::size_t operator()(SymbolStr str) const
        {
            return Key::HashType{}(str.s->string_view());
        }

        std::size_t operator()(const Key & key) const noexcept
        {
            return key.hash;
        }
    };

    struct Equal
    {
        using is_transparent = void;

        bool operator()(SymbolStr a, SymbolStr b) const noexcept
        {
            // strings are unique, so that a pointer comparison is OK
            return a.s == b.s;
        }

        bool operator()(SymbolStr a, const Key & b) const noexcept
        {
            return a == b.s;
        }

        [[gnu::always_inline]]
        bool operator()(const Key & a, SymbolStr b) const noexcept
        {
            return operator()(b, a);
        }
    };
};

class SymbolTable;

/**
 * Convenience class to statically assign symbol identifiers at compile-time.
 */
class StaticSymbolTable
{
    static constexpr std::size_t maxSize = 1024;

    struct StaticSymbolInfo
    {
        std::string_view str;
        Symbol sym;
    };

    std::array<StaticSymbolInfo, maxSize> symbols;
    std::size_t size = 0;

public:
    constexpr StaticSymbolTable() = default;

    constexpr Symbol create(std::string_view str)
    {
        /* No need to check bounds because out of bounds access is
           a compilation error. */
        auto sym = Symbol(size + 1); //< +1 because Symbol with id = 0 is reserved
        symbols[size++] = {str, sym};
        return sym;
    }

    void copyIntoSymbolTable(SymbolTable & symtab) const;
};

/**
 * Symbol table used by the parser and evaluator to represent and look
 * up identifiers and attributes efficiently.
 */
class SymbolTable
{
private:
    /* Thread-safe fallback resource, which might be a bit slower. */
    std::pmr::synchronized_pool_resource fallbackResource{};
    /**
     * SymbolTable is an append only data structure.
     * During its lifetime the monotonic buffer holds all strings and nodes, if the symbol set is node based.
     */
    BumpMemoryResource buffer{BumpMemoryResource::defaultReserveSize, &fallbackResource};
    SymbolStr::SymbolValueStore store;

    /**
     * Transparent lookup of string view for a pointer to a ChunkedVector entry -> return offset into the store.
     * ChunkedVector references are never invalidated.
     */
    boost::concurrent_flat_set<SymbolStr, SymbolStr::Hash, SymbolStr::Equal> symbols{SymbolStr::chunkSize};

public:
    SymbolTable(const StaticSymbolTable & staticSymtab)
    {
        staticSymtab.copyIntoSymbolTable(*this);
    }

    /**
     * Converts a string into a symbol.
     */
    Symbol create(std::string_view s)
    {
        Symbol res;
        symbols.insert_and_cvisit(
            SymbolStr::Key{store, s, buffer},
            [&](const auto & key) { res = Symbol(key); },
            [&](const auto & key) { res = Symbol(key); });
        return res;
    }

    /**
     * Converts a forced string value without context into a symbol. A string
     * that comes from the symbol table carries the id of its symbol, so it
     * needs no lookup.
     */
    Symbol create(const Value & v)
    {
        if (auto symbol = v.symbol())
            return symbol;
        return create(v.string_view());
    }

    std::vector<SymbolStr> resolve(const std::span<const Symbol> & symbols) const
    {
        std::vector<SymbolStr> result;
        result.reserve(symbols.size());
        for (auto sym : symbols)
            result.push_back((*this)[sym]);
        return result;
    }

    SymbolStr operator[](Symbol s) const
    {
        auto idx = s.id;
        if (--idx >= store.size())
            unreachable();

        /* Since ChunkedVector itself doesn't provide happens-before guarantees
           about the object construction, we rely on other synchronisation
           primitives to ensure happens-before relation between insertion
           into the ChunkedVector and the load here. If another thread interns
           the symbol via the symbols map, then the happens-before guarantee
           should be provided by the concurrent_map itself. If thread B gets
           Symbol from thread A through other means then it should also ensure
           happens-before relation. */

        return store[idx];
    }

    [[gnu::always_inline]]
    size_t size() const noexcept
    {
        return store.size();
    }

    size_t totalSize() const;

    template<typename T>
    void dump(T callback) const
    {
        store.forEach([&](const Value & v) { callback(SymbolStr(v)); });
    }
};

inline void StaticSymbolTable::copyIntoSymbolTable(SymbolTable & symtab) const
{
    for (std::size_t i = 0; i < size; ++i) {
        auto [str, staticSym] = symbols[i];
        auto sym = symtab.create(str);
        if (sym != staticSym) [[unlikely]]
            unreachable();
    }
}

} // namespace nix

template<>
struct std::hash<nix::Symbol>
{
    std::size_t operator()(const nix::Symbol & s) const noexcept
    {
        return std::hash<decltype(s.id)>{}(s.id);
    }
};
