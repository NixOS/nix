#pragma once

#include "nixexpr.hh"
#include "symbol-table.hh"

#include <algorithm>
#include <optional>

namespace nix {


class EvalState;
struct Value;

/* Map one attribute name to its value. */
struct Attr
{
    Symbol name;
    Value * value;
    ptr<Pos> pos;
    Attr(Symbol name, Value * value, ptr<Pos> pos = ptr(&noPos))
        : name(name), value(value), pos(pos) { };
    Attr() : pos(&noPos) { };
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
    ptr<Pos> pos;

private:
    size_t size_, capacity_;
    Attr attrs[0];

    Bindings(size_t capacity) : pos(&noPos), size_(0), capacity_(capacity) { }
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
        Attr key(name, 0);
        iterator i = std::lower_bound(begin(), end(), key);
        if (i != end() && i->name == name) return i;
        return end();
    }

    Attr * get(const Symbol & name)
    {
        Attr key(name, 0);
        iterator i = std::lower_bound(begin(), end(), key);
        if (i != end() && i->name == name) return &*i;
        return nullptr;
    }

    Attr & need(const Symbol & name, const Pos & pos = noPos)
    {
        auto a = get(name);
        if (!a)
            throw Error({
                .msg = hintfmt("attribute '%s' missing", name),
                .errPos = pos
            });

        return *a;
    }

    iterator begin() { return &attrs[0]; }
    iterator end() { return &attrs[size_]; }

    Attr & operator[](size_t pos)
    {
        return attrs[pos];
    }

    void sort();

    size_t capacity() { return capacity_; }

    /* Returns the attributes in lexicographically sorted order. */
    std::vector<const Attr *> lexicographicOrder() const
    {
        std::vector<const Attr *> res;
        res.reserve(size_);
        for (size_t n = 0; n < size_; n++)
            res.emplace_back(&attrs[n]);
        std::sort(res.begin(), res.end(), [](const Attr * a, const Attr * b) {
            return (const std::string &) a->name < (const std::string &) b->name;
        });
        return res;
    }

    friend class EvalState;
};

/* A wrapper around Bindings that ensures that its always in sorted
   order at the end. The only way to consume a BindingsBuilder is to
   call finish(), which sorts the bindings. */
class BindingsBuilder
{
    Bindings * bindings;

public:
    // needed by std::back_inserter
    using value_type = Attr;

    EvalState & state;

    BindingsBuilder(EvalState & state, Bindings * bindings)
        : bindings(bindings), state(state)
    { }

    void insert(Symbol name, Value * value, ptr<Pos> pos = ptr(&noPos))
    {
        insert(Attr(name, value, pos));
    }

    void insert(const Attr & attr)
    {
        push_back(attr);
    }

    void push_back(const Attr & attr)
    {
        bindings->push_back(attr);
    }

    Value & alloc(const Symbol & name, ptr<Pos> pos = ptr(&noPos));

    Value & alloc(std::string_view name, ptr<Pos> pos = ptr(&noPos));

    Bindings * finish()
    {
        bindings->sort();
        return bindings;
    }

    Bindings * alreadySorted()
    {
        return bindings;
    }
};

}
