#pragma once

#include "nixexpr.hh"
#include "symbol-table.hh"

#include <algorithm>

namespace nix {


class EvalState;
struct Value;

/* Map one attribute name to its value. */
struct Attr
{
    Symbol name;
    Value * value;
    const Pos * pos;
    Attr(Symbol name, Value * value, const Pos * pos)
        : name(name), value(value), pos(pos) { };
    Attr(const Pos * pos) : pos(pos) { };
    bool operator < (const Attr & a) const
    {
        return name < a.name;
    }
};

/* Bindings contains all the attributes of an attribute set. It is defined
   by its size and its capacity, the capacity being the number of Attr
   elements allocated after this structure, while the size corresponds to
   the number of elements already inserted in this structure. */
class Bindings
{
public:
    typedef uint32_t size_t;

private:
    size_t size_, capacity_;
    Attr attrs[0];

    Bindings(size_t capacity) : size_(0), capacity_(capacity) { }
    Bindings(const Bindings & bindings) = delete;

public:
    size_t size() const { return size_; }

    bool empty() const { return !size_; }

    typedef Attr * iterator;

    void push_back(const Attr & attr)
    {
        assert(size_ < capacity_);
        attrs[size_++] = attr;
    }

    iterator find(const Symbol & name)
    {
        Attr key(name, 0, 0);
        iterator i = std::lower_bound(begin(), end(), key);
        if (i != end() && i->name == name) return i;
        return end();
    }

    iterator begin() { return &attrs[0]; }
    iterator end() { return &attrs[size_]; }

    Attr & operator[](size_t pos)
    {
        return attrs[pos];
    }

    void sort();

    size_t capacity() { return capacity_; }

    friend class EvalState;
};


}
