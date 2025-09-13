#pragma once
///@file

#include "nix/expr/nixexpr.hh"
#include "nix/expr/symbol-table.hh"

#include <algorithm>
#include <functional>

namespace nix {

class EvalState;
struct Value;

/**
 * Map one attribute name to its value.
 */
struct Attr
{
    /* the placement of `name` and `pos` in this struct is important.
       both of them are uint32 wrappers, they are next to each other
       to make sure that Attr has no padding on 64 bit machines. that
       way we keep Attr size at two words with no wasted space. */
    Symbol name;
    PosIdx pos;
    Value * value = nullptr;
    Attr(Symbol name, Value * value, PosIdx pos = noPos)
        : name(name)
        , pos(pos)
        , value(value) {};
    Attr() {};

    auto operator<=>(const Attr & a) const
    {
        return name <=> a.name;
    }
};

static_assert(
    sizeof(Attr) == 2 * sizeof(uint32_t) + sizeof(Value *),
    "performance of the evaluator is highly sensitive to the size of Attr. "
    "avoid introducing any padding into Attr if at all possible, and do not "
    "introduce new fields that need not be present for almost every instance.");

/**
 * Bindings contains all the attributes of an attribute set. It is defined
 * by its size and its capacity, the capacity being the number of Attr
 * elements allocated after this structure, while the size corresponds to
 * the number of elements already inserted in this structure.
 */
class Bindings
{
public:
    typedef uint32_t size_t;
    PosIdx pos;

    /**
     * An instance of bindings objects with 0 attributes.
     * This object must never be modified.
     */
    static Bindings emptyBindings;

private:
    size_t size_ = 0;
    Attr attrs[0];

    Bindings() = default;
    Bindings(const Bindings &) = delete;
    Bindings(Bindings &&) = delete;
    Bindings & operator=(const Bindings &) = delete;
    Bindings & operator=(Bindings &&) = delete;

public:
    size_t size() const
    {
        return size_;
    }

    bool empty() const
    {
        return !size_;
    }

    typedef const Attr * const_iterator;

    void push_back(const Attr & attr)
    {
        attrs[size_++] = attr;
    }

    const_iterator find(Symbol name) const
    {
        Attr key(name, 0);
        const_iterator i = std::lower_bound(begin(), end(), key);
        if (i != end() && i->name == name)
            return i;
        return end();
    }

    const Attr * get(Symbol name) const
    {
        Attr key(name, 0);
        const_iterator i = std::lower_bound(begin(), end(), key);
        if (i != end() && i->name == name)
            return &*i;
        return nullptr;
    }

    const_iterator begin() const
    {
        return &attrs[0];
    }

    const_iterator end() const
    {
        return &attrs[size_];
    }

    Attr & operator[](size_t pos)
    {
        return attrs[pos];
    }

    const Attr & operator[](size_t pos) const
    {
        return attrs[pos];
    }

    void sort();

    /**
     * Returns the attributes in lexicographically sorted order.
     */
    std::vector<const Attr *> lexicographicOrder(const SymbolTable & symbols) const
    {
        std::vector<const Attr *> res;
        res.reserve(size_);
        for (size_t n = 0; n < size_; n++)
            res.emplace_back(&attrs[n]);
        std::sort(res.begin(), res.end(), [&](const Attr * a, const Attr * b) {
            std::string_view sa = symbols[a->name], sb = symbols[b->name];
            return sa < sb;
        });
        return res;
    }

    friend class EvalState;
};

/**
 * A wrapper around Bindings that ensures that its always in sorted
 * order at the end. The only way to consume a BindingsBuilder is to
 * call finish(), which sorts the bindings.
 */
class BindingsBuilder final
{
public:
    // needed by std::back_inserter
    using value_type = Attr;
    using size_type = Bindings::size_t;

private:
    Bindings * bindings;
    Bindings::size_t capacity_;

    friend class EvalState;

    BindingsBuilder(EvalState & state, Bindings * bindings, size_type capacity)
        : bindings(bindings)
        , capacity_(capacity)
        , state(state)
    {
    }

public:
    std::reference_wrapper<EvalState> state;

    void insert(Symbol name, Value * value, PosIdx pos = noPos)
    {
        insert(Attr(name, value, pos));
    }

    void insert(const Attr & attr)
    {
        push_back(attr);
    }

    void push_back(const Attr & attr)
    {
        assert(bindings->size() < capacity_);
        bindings->push_back(attr);
    }

    Value & alloc(Symbol name, PosIdx pos = noPos);

    Value & alloc(std::string_view name, PosIdx pos = noPos);

    Bindings * finish()
    {
        bindings->sort();
        return bindings;
    }

    Bindings * alreadySorted()
    {
        return bindings;
    }

    size_t capacity() const noexcept
    {
        return capacity_;
    }

    void grow(BindingsBuilder newBindings)
    {
        for (auto & i : *bindings)
            newBindings.push_back(i);
        std::swap(*this, newBindings);
    }

    friend struct ExprAttrs;
};

} // namespace nix
