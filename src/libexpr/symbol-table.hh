#pragma once

#include <map>
#include <unordered_set>

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
    const string * s; // pointer into SymbolTable
    Symbol(const string * s) : s(s) { };
    friend class SymbolTable;

public:
    Symbol() : s(0) { };

    bool operator == (const Symbol & s2) const
    {
        return s == s2.s;
    }

    bool operator != (const Symbol & s2) const
    {
        return s != s2.s;
    }

    bool operator < (const Symbol & s2) const
    {
        return s < s2.s;
    }

    operator const string & () const
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
    typedef std::unordered_set<string> Symbols;
    Symbols symbols;

public:
    Symbol create(const string & s)
    {
        std::pair<Symbols::iterator, bool> res = symbols.insert(s);
        return Symbol(&*res.first);
    }

    size_t size() const
    {
        return symbols.size();
    }

    size_t totalSize() const;

    template<typename T>
    void dump(T callback)
    {
        for (auto & s : symbols)
            callback(s);
    }
};

}
