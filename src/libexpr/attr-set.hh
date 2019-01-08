#pragma once

#include "nixexpr.hh"
#include "symbol-table.hh"

#include <algorithm>

namespace nix {

struct Value;


/* Bindings contains all the attributes of an attribute set. */
class Bindings
{
    friend class BindingsBuilder;
    friend size_t valueSize(Value & v);

    class Names {
        mutable unsigned int * _lexicographicOrder = nullptr; // memoized indexes in names[],values[],positions[] in lexicographic order of names[]
                                                              // TODO: memoize `attrNames' result (which also can be reused in mapAttrs)
    public:
        std::vector<Symbol> symOrder;

        ~Names() {
            assert(_lexicographicOrder == nullptr); // interned `Names' are never destructed
        }                                           // `Names' in BindingsBuilder are not complete and should not use `lexicographicOrder()'

        // exclude mutables
        bool operator==(const Names & that) const { return symOrder == that.symOrder; }

        const unsigned int * lexicographicOrder() const {
            if (_lexicographicOrder == nullptr) {
                unsigned int * p = new unsigned int[symOrder.size()];
                for (unsigned int i = 0; i < symOrder.size(); ++i)
                    p[i] = i;

                std::sort(&p[0], &p[symOrder.size()], [&](unsigned int a, unsigned int b) {
                    return (const string &)symOrder[a] < (const string &)symOrder[b];
                });
                _lexicographicOrder = p;
            }
            return _lexicographicOrder;
        }
    };

    const Names *                    pnames;     // interned in Bindings::pnamesTable
    const std::vector<const Pos *> * ppositions; // interned in Bindings::pposTable
    Value *                          values[0];

    Bindings(const Names * pnames, const std::vector<const Pos *> * ppositions): pnames(pnames), ppositions(ppositions) { }
    Bindings(const Bindings & bindings) = delete;
    Bindings() = delete;

    struct NamesHasher
    {
        size_t operator()(const Names & names) const
        {
            size_t result = names.symOrder.size();
            for (Symbol s : names.symOrder) {
              const string * pstr = &(const string &)s;
              result *= 31;
              result += reinterpret_cast<size_t>(pstr);
            }
            return result;
        }
    };

    struct PosHasher
    {
        size_t operator()(const std::vector<const Pos *> & v) const
        {
            size_t result = v.size();
            for (const Pos * p : v) {
              result *= 31;
              result += reinterpret_cast<size_t>(p);
            }
            return result;
        }
    };

public:
    static std::unordered_set<Names, NamesHasher>                  * pnamesTable; // the life time of intern hashsets is the life time of the nix process.
    static std::unordered_set<std::vector<const Pos *>, PosHasher> * pposTable;   // pointers just to let them leak to prevent spending time on destruction 
                                                                                  // of thousands of small objects at the end of the process.

    const static Bindings * zero;
    static int nNameLists;
    static int bNameLists;
    static int nPosLists;
    static int bPosLists;

    inline bool empty() const { return pnames->symOrder.empty(); }

    inline unsigned int size() const { return pnames->symOrder.size(); }

    inline bool sameKeys(const Bindings * that) const { return this->pnames == that->pnames; }

    // limited "iterator" returned by .find(), it has no prev/next
    class find_iterator
    {
        const Bindings * const outer;
        const unsigned int     index;
    public:
        find_iterator(const Bindings * outer, unsigned int index): outer(outer), index(index) { }
        inline Symbol      name()  const { return outer->pnames->symOrder[index]; }
        inline Value *     value() const { return outer->values          [index]; }
        inline const Pos * pos()   const { return (*outer->ppositions)   [index]; }
        inline bool        found() const { return outer != nullptr; }
    };

    inline find_iterator find(Symbol name) const
    {
        std::vector<Symbol>::const_iterator i = std::lower_bound(pnames->symOrder.begin(), pnames->symOrder.end(), name);
        if (i != pnames->symOrder.end() && *i == name) {
            return find_iterator(this, (unsigned int)(i - pnames->symOrder.begin()));
        }
        return find_iterator(nullptr, 0);
    }

    // iterate in order of Symbol pointer values
    class iterator
    {
        const Bindings * const outer;
        unsigned int           index;
    public:
        iterator(const Bindings * outer, unsigned int index): outer(outer), index(index) { }
        inline iterator &  operator++()       { ++index; return *this; }
        inline bool        at_end()     const { return index == outer->size(); }
        inline Symbol      name()       const { return outer->pnames->symOrder[index]; }
        inline Value *     value()      const { return outer->values          [index]; }
        inline const Pos * pos()        const { return (*outer->ppositions)   [index]; }
    };

    inline iterator begin() const { return iterator(this, 0); }

    // iterate in lexicographic order
    class lex_iterator
    {
        const Bindings * const      outer;
        const unsigned int * const  lex_order;
        unsigned int                lex_index;
    public:
        lex_iterator(const Bindings * outer, const unsigned int * lex_order, unsigned int lex_index): outer(outer), lex_order(lex_order), lex_index(lex_index) { }
        inline lex_iterator & operator++()        { ++lex_index; return *this; }
        inline bool           at_end()      const { return lex_index == outer->size(); }
        inline Symbol         name()        const { return outer->pnames->symOrder[lex_order[lex_index]];  }
        inline Value *        value()       const { return outer->values          [lex_order[lex_index]];  }
        inline const Pos *    pos()         const { return (*outer->ppositions)   [lex_order[lex_index]];  }
    };

    inline lex_iterator lex_begin() const { return lex_iterator(this, pnames->lexicographicOrder(), 0); }
};


class BindingsBuilder
{
public:
    Bindings::Names          names;
    ValueVector              values;
    std::vector<const Pos *> positions;
#ifndef _NDEBUG
    bool                     resultCalled = false;
#endif

    BindingsBuilder(unsigned int initial_capacity) {
        names.symOrder.reserve(initial_capacity);
        values.reserve(initial_capacity);
        positions.reserve(initial_capacity);
    }

    void push_back(Symbol name, Value * value, const Pos * pos /*= &noPos*/)
    {
        names.symOrder.emplace_back(name);
        values.emplace_back(value);
        positions.emplace_back(pos);
    }

    typedef unsigned int iterator;

    inline unsigned int size()  const { assert(!resultCalled); return names.symOrder.size(); }
    inline iterator     begin() const { assert(!resultCalled); return iterator{0};      }
    inline iterator     end()   const { assert(!resultCalled); return iterator{size()}; }

    iterator find(Symbol name)
    {
        for (unsigned int i=0; i<size(); i++) { // full search, data is not yet sorted
            if (name == names.symOrder[i])
                return iterator{i};
        }
        return end();
    }

    Bindings * result(bool alreadySorted = false);
private:
    Bindings * resultAt(void * p, bool alreadySorted = false);
    void quicksort(iterator a, iterator b);
};


}
