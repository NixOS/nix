#pragma once

#include "config.h"

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

    unsigned int size() const
    {
        return symbols.size();
    }

    size_t totalSize() const;
};

/* Position objects. */

struct Pos
{
    Symbol file;
    unsigned int firstline, firstcolumn, lastline, lastcolumn;
    Pos() : firstline(0), firstcolumn(0), lastline(-1),lastcolumn(-1) { };
    Pos(const Symbol & file, unsigned int firstline, unsigned int firstcolumn, unsigned int lastline, unsigned int lastcolumn)
        : file(file), firstline(firstline), firstcolumn(firstcolumn), lastline(lastline), lastcolumn(lastcolumn) { };
    operator bool() const
    {
        return firstline != 0;
    }
    bool operator < (const Pos & p2) const
    {
        if (!firstline) return p2.firstline;
        if (!p2.firstline) return false;
        int d = ((string) file).compare((string) p2.file);
        if (d < 0) return true;
        if (d > 0) return false;
        if (firstline < p2.firstline) return true;
        if (firstline > p2.firstline) return false;
        if (firstcolumn < p2.firstcolumn) return true;
        if (firstcolumn > p2.firstcolumn) return false;
        if (lastline < p2.lastline) return true;
        if (lastline > p2.lastline) return false;
        return lastcolumn < p2.lastcolumn;
    }
};

extern Pos noPos;

}
