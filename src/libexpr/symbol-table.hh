#pragma once
///@file

#include <array>
#include <unordered_map>

#include "types.hh"
#include "sync.hh"

namespace nix {

struct ContiguousArena
{
    const char * data;
    const size_t maxSize;
    std::atomic<size_t> size{0};

    ContiguousArena(size_t maxSize);

    size_t allocate(size_t bytes);
};

/**
 * This class mainly exists to give us an operator<< for ostreams. We could also
 * return plain strings from SymbolTable, but then we'd have to wrap every
 * instance of a symbol that is fmt()ed, which is inconvenient and error-prone.
 */
class SymbolStr
{
    friend class SymbolTable;

private:
    std::string_view s;

    explicit SymbolStr(std::string_view s): s(s) {}

public:
    bool operator == (std::string_view s2) const
    {
        return s == s2;
    }

    const char * c_str() const
    {
        return s.data();
    }

    operator const std::string_view () const
    {
        return s;
    }

    friend std::ostream & operator <<(std::ostream & os, const SymbolStr & symbol);
};

/**
 * Symbols have the property that they can be compared efficiently
 * (using an equality test), because the symbol table stores only one
 * copy of each string.
 */
class Symbol
{
    friend class SymbolTable;

private:
    /// The offset of the symbol in `SymbolTable::arena`.
    uint32_t id;

    explicit Symbol(uint32_t id): id(id) {}

public:
    Symbol() : id(0) {}

    explicit operator bool() const { return id > 0; }

    bool operator<(const Symbol other) const { return id < other.id; }
    bool operator==(const Symbol other) const { return id == other.id; }
    bool operator!=(const Symbol other) const { return id != other.id; }

    friend class std::hash<Symbol>;
};

/**
 * Symbol table used by the parser and evaluator to represent and look
 * up identifiers and attributes efficiently.
 */
class SymbolTable
{
private:
    std::array<SharedSync<std::unordered_map<std::string_view, uint32_t>>, 32> symbolDomains;
    ContiguousArena arena;

public:

    SymbolTable()
        : arena(1 << 30)
    {
        // Reserve symbol ID 0.
        arena.allocate(1);
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
        if (s.id == 0 || s.id > arena.size)
            abort();
        return SymbolStr(std::string_view(arena.data + s.id));
    }

    size_t size() const;

    size_t totalSize() const
    {
        return arena.size;
    }

    template<typename T>
    void dump(T callback) const
    {
        // FIXME
        //state_.read()->store.forEach(callback);
    }
};

}

template<>
struct std::hash<nix::Symbol>
{
    std::size_t operator()(const nix::Symbol & s) const noexcept
    {
        return std::hash<decltype(s.id)>{}(s.id);
    }
};
