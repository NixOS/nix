#pragma once
///@file

#include <memory_resource>

#include "nix/expr/value.hh"
#include "nix/util/error.hh"
#include "nix/util/sync.hh"

#include <boost/version.hpp>
#include <boost/unordered/concurrent_flat_set.hpp>

namespace nix {

class SymbolValue : protected Value
{
    friend class SymbolStr;
    friend class SymbolTable;

    operator std::string_view() const noexcept
    {
        // The actual string is stored directly after the value.
        return reinterpret_cast<const char *>(this + 1);
    }
};

struct ContiguousArena
{
    const char * data;
    const size_t maxSize;

    // Put this in a separate cache line to ensure that a thread
    // adding a symbol doesn't slow down threads dereferencing symbols
    // by invalidating the read-only `data` field.
    alignas(64) std::atomic<size_t> size{0};

    ContiguousArena(size_t maxSize);

    size_t allocate(size_t bytes);
};

/**
 * Symbols have the property that they can be compared efficiently
 * (using an equality test), because the symbol table stores only one
 * copy of each string.
 */
class Symbol
{
    friend class SymbolStr;
    friend class SymbolTable;

private:
    /// The offset of the symbol in `SymbolTable::arena`.
    uint32_t id;

    explicit Symbol(uint32_t id) noexcept
        : id(id)
    {
    }

public:
    Symbol() noexcept
        : id(0)
    {
    }

    [[gnu::always_inline]]
    explicit operator bool() const noexcept
    {
        return id > 0;
    }

    auto operator<=>(const Symbol other) const noexcept
    {
        return id <=> other.id;
    }

    bool operator==(const Symbol other) const noexcept
    {
        return id == other.id;
    }

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

    const SymbolValue * s;

    struct Key
    {
        using HashType = boost::hash<std::string_view>;

        std::string_view s;
        std::size_t hash;
        ContiguousArena & arena;

        Key(std::string_view s, ContiguousArena & arena)
            : s(s)
            , hash(HashType{}(s))
            , arena(arena)
        {
        }
    };

public:
    SymbolStr(const SymbolValue & s) noexcept
        : s(&s)
    {
    }

    SymbolStr(const Key & key);

    bool operator==(std::string_view s2) const noexcept
    {
        return *s == s2;
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
        return static_cast<std::string_view>(*s).empty();
    }

    [[gnu::always_inline]]
    size_t size() const noexcept
    {
        return static_cast<std::string_view>(*s).size();
    }

    [[gnu::always_inline]]
    const Value * valuePtr() const noexcept
    {
        return s;
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
    ContiguousArena arena;

    /**
     * Transparent lookup of string view for a pointer to a
     * SymbolValue in the arena.
     */
    boost::concurrent_flat_set<SymbolStr, SymbolStr::Hash, SymbolStr::Equal> symbols;

public:

    constexpr static size_t alignment = alignof(SymbolValue);

    SymbolTable()
        : arena(1 << 30)
    {
        // Reserve symbol ID 0 and ensure alignment of the first allocation.
        arena.allocate(alignment);
    }

    /**
     * Converts a string into a symbol.
     */
    Symbol create(std::string_view s);

    std::vector<SymbolStr> resolve(const std::vector<Symbol> & symbols) const
    {
        std::vector<SymbolStr> result;
        result.reserve(symbols.size());
        for (auto & sym : symbols)
            result.push_back((*this)[sym]);
        return result;
    }

    SymbolStr operator[](Symbol s) const
    {
        assert(s.id);
        // Note: we don't check arena.size here to avoid a dependency
        // on other threads creating new symbols.
        return SymbolStr(*reinterpret_cast<const SymbolValue *>(arena.data + s.id));
    }

    size_t size() const noexcept
    {
        return symbols.size();
    }

    size_t totalSize() const
    {
        return arena.size;
    }

    template<typename T>
    void dump(T callback) const
    {
        std::string_view left{arena.data, arena.size};
        left = left.substr(alignment);
        while (true) {
            if (left.empty())
                break;
            left = left.substr(sizeof(Value));
            auto p = left.find('\0');
            assert(p != left.npos);
            auto sym = left.substr(0, p);
            callback(sym);
            // skip alignment padding
            auto n = sym.size() + 1;
            left = left.substr(n + (n % alignment ? alignment - (n % alignment) : 0));
        }
    }
};

} // namespace nix

template<>
struct std::hash<nix::Symbol>
{
    std::size_t operator()(const nix::Symbol & s) const noexcept
    {
        return std::hash<decltype(s.id)>{}(s.id);
    }
};
