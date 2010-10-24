#ifndef __SYMBOL_TABLE_H
#define __SYMBOL_TABLE_H

#include "config.h"

#include <map>

#if HAVE_TR1_UNORDERED_SET
#include <tr1/unordered_set>
#endif

#include "types.hh"

namespace nix {

/* Symbol table used by the parser and evaluator to represent and look
   up identifiers and attribute sets efficiently.
   SymbolTable::create() converts a string into a symbol.  Symbols
   have the property that they can be compared efficiently (using a
   pointer equality test), because the symbol table stores only one
   copy of each string. */

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

    bool empty() const
    {
        return s->empty();
    }

    friend std::ostream & operator << (std::ostream & str, const Symbol & sym);
};

inline std::ostream & operator << (std::ostream & str, const Symbol & sym)
{
    str << *sym.s;
    return str;
}

class SymbolTable
{
private:
#if HAVE_TR1_UNORDERED_SET 
    typedef std::tr1::unordered_set<string> Symbols;
#else
    typedef std::set<string> Symbols;
#endif
    Symbols symbols;

public:
    Symbol create(const string & s)
    {
        std::pair<Symbols::iterator, bool> res = symbols.insert(s);
        return Symbol(&*res.first);
    }

    unsigned int size() const
    {
        return symbols.size();
    }
};

}

#endif /* !__SYMBOL_TABLE_H */
