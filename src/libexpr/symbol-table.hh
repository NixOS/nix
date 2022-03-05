#pragma once

#include <list>
#include <map>
#include <unordered_map>

#include "types.hh"

namespace nix {

/* Symbol table used by the parser and evaluator to represent and look
   up identifiers and attributes efficiently.  SymbolTable::create()
   converts a string into a symbol.  Symbols have the property that
   they can be compared efficiently (using a pointer equality test),
   because the symbol table stores only one copy of each string. */

class Symbol
{
    friend class SymbolTable;
private:
    std::string s;

public:
    Symbol(std::string_view s) : s(s) { }

    // FIXME: remove
    bool operator == (std::string_view s2) const
    {
        return s == s2;
    }

    operator const std::string & () const
    {
        return s;
    }

    operator const std::string_view () const
    {
        return s;
    }

    friend std::ostream & operator << (std::ostream & str, const Symbol & sym);
};

class SymbolIdx
{
    friend class SymbolTable;

private:
    uint32_t id;

    explicit SymbolIdx(uint32_t id): id(id) {}

public:
    SymbolIdx() : id(0) {}

    explicit operator bool() const { return id > 0; }

    bool operator<(const SymbolIdx other) const { return id < other.id; }
    bool operator==(const SymbolIdx other) const { return id == other.id; }
    bool operator!=(const SymbolIdx other) const { return id != other.id; }
};

class SymbolTable
{
private:
    std::unordered_map<std::string_view, std::pair<const Symbol *, uint32_t>> symbols;
    ChunkedVector<Symbol, 8192> store{16};

public:
    SymbolIdx create(std::string_view s)
    {
        // Most symbols are looked up more than once, so we trade off insertion performance
        // for lookup performance.
        // TODO: could probably be done more efficiently with transparent Hash and Equals
        // on the original implementation using unordered_set
        auto it = symbols.find(s);
        if (it != symbols.end()) return SymbolIdx(it->second.second + 1);

        const auto & [rawSym, idx] = store.add(s);
        symbols.emplace(rawSym, std::make_pair(&rawSym, idx));
        return SymbolIdx(idx + 1);
    }

    const Symbol & operator[](SymbolIdx s) const
    {
        if (s.id == 0 || s.id > store.size())
            abort();
        return store[s.id - 1];
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
