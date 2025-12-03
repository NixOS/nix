#pragma once
///@file

#include <memory_resource>
#include "nix/expr/value.hh"
#include "nix/expr/static-string-data.hh"
#include "nix/util/chunked-vector.hh"
#include "nix/util/error.hh"

#include <boost/version.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

namespace nix {

class SymbolValue : protected Value
{
    friend class SymbolStr;
    friend class SymbolTable;

    uint32_t idx;

    SymbolValue() = default;

public:
    operator std::string_view() const noexcept
    {
        return string_view();
    }
};

class StaticSymbolTable;

/**
 * Symbols have the property that they can be compared efficiently
 * (using an equality test), because the symbol table stores only one
 * copy of each string.
 */
class Symbol
{
    friend class SymbolStr;
    friend class SymbolTable;
    friend class StaticSymbolTable;

private:
    uint32_t id;

    explicit constexpr Symbol(uint32_t id) noexcept
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
    constexpr uint32_t getId() const noexcept
    {
        return id;
    }

    constexpr auto operator<=>(const Symbol & other) const noexcept = default;

    friend class std::hash<Symbol>;
};

/**
 * This class mainly exists to give us an operator<< for ostreams. We could also
 * return plain strings from SymbolTable, but then we'd have to wrap every
 * instance of a symbol that is fmt()ed, which is inconvenient and error-prone.
 */
class SymbolStr
{
    friend class SymbolTable;

    constexpr static size_t chunkSize{8192};
    using SymbolValueStore = ChunkedVector<SymbolValue, chunkSize>;

    const SymbolValue * s;

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
    SymbolStr(const SymbolValue & s) noexcept
        : s(&s)
    {
    }

    SymbolStr(const Key & key)
    {
        auto size = key.s.size();
        if (size >= std::numeric_limits<uint32_t>::max()) {
            throw Error("Size of symbol exceeds 4GiB and cannot be stored");
        }
        // for multi-threaded implementations: lock store and allocator here
        const auto & [v, idx] = key.store.add(SymbolValue{});
        if (size == 0) {
            v.mkStringNoCopy(""_sds, nullptr);
        } else {
            v.mkStringNoCopy(StringData::make(key.resource, key.s));
        }
        v.idx = idx;
        this->s = &v;
    }

    bool operator==(std::string_view s2) const noexcept
    {
        return *s == s2;
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
        return *s;
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
        return Symbol{s->idx + 1};
    }

    struct Hash
    {
        using is_transparent = void;
        using is_avalanching = std::true_type;

        std::size_t operator()(SymbolStr str) const
        {
            return Key::HashType{}(*str.s);
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
    /**
     * SymbolTable is an append only data structure.
     * During its lifetime the monotonic buffer holds all strings and nodes, if the symbol set is node based.
     */
    std::pmr::monotonic_buffer_resource buffer;
    SymbolStr::SymbolValueStore store{16};

    /**
     * Transparent lookup of string view for a pointer to a ChunkedVector entry -> return offset into the store.
     * ChunkedVector references are never invalidated.
     */
    boost::unordered_flat_set<SymbolStr, SymbolStr::Hash, SymbolStr::Equal> symbols{SymbolStr::chunkSize};

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
        // Most symbols are looked up more than once, so we trade off insertion performance
        // for lookup performance.
        // FIXME: make this thread-safe.
        return Symbol(*symbols.insert(SymbolStr::Key{store, s, buffer}).first);
    }

    std::vector<SymbolStr> resolve(const std::vector<Symbol> & symbols) const
    {
        std::vector<SymbolStr> result;
        result.reserve(symbols.size());
        for (auto sym : symbols)
            result.push_back((*this)[sym]);
        return result;
    }

    SymbolStr operator[](Symbol s) const
    {
        uint32_t idx = s.id - uint32_t(1);
        if (idx >= store.size())
            unreachable();
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
        store.forEach(callback);
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
