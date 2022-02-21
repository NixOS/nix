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
private:
    const std::string * s; // pointer into SymbolTable
    Symbol(const std::string * s) : s(s) { };
    friend class SymbolTable;

public:
    Symbol() : s(0) { };

    bool operator == (const Symbol & s2) const
    {
        return s == s2.s;
    }

    // FIXME: remove
    bool operator == (std::string_view s2) const
    {
        return s->compare(s2) == 0;
    }

    bool operator != (const Symbol & s2) const
    {
        return s != s2.s;
    }

    bool operator < (const Symbol & s2) const
    {
        return s < s2.s;
    }

    operator const std::string & () const
    {
        return *s;
    }

    operator const std::string_view () const
    {
        return *s;
    }

    bool set() const
    {
        return s;
    }

    bool empty() const
    {
        return s->empty();
    }

    friend std::ostream & operator << (std::ostream & str, const Symbol & sym);
};

class SymbolTable
{
private:
    std::unordered_map<std::string_view, Symbol> symbols;
    std::list<std::string> store;

public:
    Symbol create(std::string_view s)
    {
        // Most symbols are looked up more than once, so we trade off insertion performance
        // for lookup performance.
        // TODO: could probably be done more efficiently with transparent Hash and Equals
        // on the original implementation using unordered_set
        auto it = symbols.find(s);
        if (it != symbols.end()) return it->second;

        auto & rawSym = store.emplace_back(s);
        return symbols.emplace(rawSym, Symbol(&rawSym)).first->second;
    }

    size_t size() const
    {
        return symbols.size();
    }

    size_t totalSize() const;

    template<typename T>
    void dump(T callback)
    {
        for (auto & s : store)
            callback(s);
    }
};

}
