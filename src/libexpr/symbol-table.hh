#pragma once

#include <list>
#include <map>
#include <unordered_map>

#include "types.hh"
#include "chunked-vector.hh"

namespace nix {

/* Symbol table used by the parser and evaluator to represent and look
   up identifiers and attributes efficiently.  SymbolTable::create()
   converts a string into a symbol.  Symbols have the property that
   they can be compared efficiently (using an equality test),
   because the symbol table stores only one copy of each string. */

/* This class mainly exists to give us an operator<< for ostreams. We could also
   return plain strings from SymbolTable, but then we'd have to wrap every
   instance of a symbol that is fmt()ed, which is inconvenient and error-prone. */
class SymbolStr
{
    friend class SymbolTable;

private:
    const std::string * s;

    explicit SymbolStr(const std::string & symbol): s(&symbol) {}

public:
    bool operator == (std::string_view s2) const
    {
        return *s == s2;
    }

    operator const std::string & () const
    {
        return *s;
    }

    operator const std::string_view () const
    {
        return *s;
    }

    friend std::ostream & operator <<(std::ostream & os, const SymbolStr & symbol);
};

class Symbol
{
    friend class SymbolTable;

private:
    uint32_t id;

    explicit Symbol(uint32_t id): id(id) {}

public:
    Symbol() : id(0) {}

    explicit operator bool() const { return id > 0; }

    bool operator<(const Symbol other) const { return id < other.id; }
    bool operator==(const Symbol other) const { return id == other.id; }
    bool operator!=(const Symbol other) const { return id != other.id; }
};

class SymbolTable
{
private:
    std::unordered_map<std::string_view, std::pair<const std::string *, uint32_t>> symbols;
    ChunkedVector<std::string, 8192> store{16};

public:
    Symbol create(std::string_view s)
    {
        // Most symbols are looked up more than once, so we trade off insertion performance
        // for lookup performance.
        // TODO: could probably be done more efficiently with transparent Hash and Equals
        // on the original implementation using unordered_set
        auto it = symbols.find(s);
        if (it != symbols.end()) return Symbol(it->second.second + 1);

        const auto & [rawSym, idx] = store.add(std::string(s));
        symbols.emplace(rawSym, std::make_pair(&rawSym, idx));
        return Symbol(idx + 1);
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
        if (s.id == 0 || s.id > store.size())
            abort();
        return SymbolStr(store[s.id - 1]);
    }

    size_t size() const
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

}
