#pragma once
///@file

#include "nix/expr/nixexpr.hh"
#include "nix/expr/symbol-table.hh"

#include <boost/container/static_vector.hpp>
#include <boost/iterator/function_output_iterator.hpp>

#include <algorithm>
#include <functional>
#include <ranges>
#include <optional>

namespace nix {

class EvalMemory;
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
 *
 * Bindings can be efficiently `//`-composed into an intrusive linked list of "layers"
 * that saves on copies and allocations. Each lookup (@see Bindings::get) traverses
 * this linked list until a matching attribute is found (thus overlays earlier in
 * the list take precedence). For iteration over the whole Bindings, an on-the-fly
 * k-way merge is performed by Bindings::iterator class.
 */
class Bindings
{
public:
    using size_type = uint32_t;

    PosIdx pos;

    /**
     * An instance of bindings objects with 0 attributes.
     * This object must never be modified.
     */
    static Bindings emptyBindings;

private:
    /**
     * Number of attributes in the attrs FAM (Flexible Array Member).
     */
    size_type numAttrs = 0;

    /**
     * Number of attributes with unique names in the layer chain.
     *
     * This is the *real* user-facing size of bindings, whereas @ref numAttrs is
     * an implementation detail of the data structure.
     */
    size_type numAttrsInChain = 0;

    /**
     * Length of the layers list.
     */
    uint32_t numLayers = 1;

    /**
     * Bindings that this attrset is "layered" on top of.
     */
    const Bindings * baseLayer = nullptr;

    /**
     * Flexible array member of attributes.
     */
    Attr attrs[0];

    Bindings() = default;
    Bindings(const Bindings &) = delete;
    Bindings(Bindings &&) = delete;
    Bindings & operator=(const Bindings &) = delete;
    Bindings & operator=(Bindings &&) = delete;

    friend class BindingsBuilder;

    /**
     * Maximum length of the Bindings layer chains.
     */
    static constexpr unsigned maxLayers = 8;

public:
    size_type size() const
    {
        return numAttrsInChain;
    }

    bool empty() const
    {
        return size() == 0;
    }

    class iterator
    {
    public:
        using value_type = Attr;
        using pointer = const value_type *;
        using reference = const value_type &;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::forward_iterator_tag;

        friend class Bindings;

    private:
        struct BindingsCursor
        {
            /**
             * Attr that the cursor currently points to.
             */
            pointer current;

            /**
             * One past the end pointer to the contiguous buffer of Attrs.
             */
            pointer end;

            /**
             * Priority of the value. Lesser values have more priority (i.e. they override
             * attributes that appear later in the linked list of Bindings).
             */
            uint32_t priority;

            pointer operator->() const noexcept
            {
                return current;
            }

            reference get() const noexcept
            {
                return *current;
            }

            bool empty() const noexcept
            {
                return current == end;
            }

            void increment() noexcept
            {
                ++current;
            }

            void consume(Symbol name) noexcept
            {
                while (!empty() && current->name <= name)
                    ++current;
            }

            GENERATE_CMP(BindingsCursor, me->current->name, me->priority)
        };

        using QueueStorageType = boost::container::static_vector<BindingsCursor, maxLayers>;

        /**
         * Comparator implementing the override priority / name ordering
         * for BindingsCursor.
         */
        static constexpr auto comp = std::greater<BindingsCursor>();

        /**
         * A priority queue used to implement an on-the-fly k-way merge.
         */
        QueueStorageType cursorHeap;

        /**
         * The attribute the iterator currently points to.
         */
        pointer current = nullptr;

        /**
         * Whether iterating over a single attribute and not a merge chain.
         */
        bool doMerge = true;

        void push(BindingsCursor cursor) noexcept
        {
            cursorHeap.push_back(cursor);
            std::ranges::make_heap(cursorHeap, comp);
        }

        [[nodiscard]] BindingsCursor pop() noexcept
        {
            std::ranges::pop_heap(cursorHeap, comp);
            auto cursor = cursorHeap.back();
            cursorHeap.pop_back();
            return cursor;
        }

        iterator & finished() noexcept
        {
            current = nullptr;
            return *this;
        }

        void next(BindingsCursor cursor) noexcept
        {
            current = &cursor.get();
            cursor.increment();

            if (!cursor.empty())
                push(cursor);
        }

        std::optional<BindingsCursor> consumeAllUntilCurrentName() noexcept
        {
            auto cursor = pop();
            Symbol lastHandledName = current->name;

            while (cursor->name <= lastHandledName) {
                cursor.consume(lastHandledName);
                if (!cursor.empty())
                    push(cursor);

                if (cursorHeap.empty())
                    return std::nullopt;

                cursor = pop();
            }

            return cursor;
        }

        explicit iterator(const Bindings & attrs) noexcept
            : doMerge(attrs.baseLayer)
        {
            auto pushBindings = [this, priority = unsigned{0}](const Bindings & layer) mutable {
                auto first = layer.attrs;
                push(
                    BindingsCursor{
                        .current = first,
                        .end = first + layer.numAttrs,
                        .priority = priority++,
                    });
            };

            if (!doMerge) {
                if (attrs.empty())
                    return;

                current = attrs.attrs;
                pushBindings(attrs);

                return;
            }

            const Bindings * layer = &attrs;
            while (layer) {
                if (layer->numAttrs != 0)
                    pushBindings(*layer);
                layer = layer->baseLayer;
            }

            if (cursorHeap.empty())
                return;

            next(pop());
        }

    public:
        iterator() = default;

        reference operator*() const noexcept
        {
            return *current;
        }

        pointer operator->() const noexcept
        {
            return current;
        }

        iterator & operator++() noexcept
        {
            if (!doMerge) {
                ++current;
                if (current == cursorHeap.front().end)
                    return finished();
                return *this;
            }

            if (cursorHeap.empty())
                return finished();

            auto cursor = consumeAllUntilCurrentName();
            if (!cursor)
                return finished();

            next(*cursor);
            return *this;
        }

        iterator operator++(int) noexcept
        {
            iterator tmp = *this;
            ++*this;
            return tmp;
        }

        bool operator==(const iterator & rhs) const noexcept
        {
            return current == rhs.current;
        }
    };

    using const_iterator = iterator;

    void push_back(const Attr & attr)
    {
        attrs[numAttrs++] = attr;
        numAttrsInChain = numAttrs;
    }

    /**
     * Get attribute by name or nullptr if no such attribute exists.
     */
    const Attr * get(Symbol name) const noexcept
    {
        auto getInChunk = [key = Attr{name, nullptr}](const Bindings & chunk) -> const Attr * {
            auto first = chunk.attrs;
            auto last = first + chunk.numAttrs;
            const Attr * i = std::lower_bound(first, last, key);
            if (i != last && i->name == key.name)
                return i;
            return nullptr;
        };

        const Bindings * currentChunk = this;
        while (currentChunk) {
            const Attr * maybeAttr = getInChunk(*currentChunk);
            if (maybeAttr)
                return maybeAttr;
            currentChunk = currentChunk->baseLayer;
        }

        return nullptr;
    }

    /**
     * Check if the layer chain is full.
     */
    bool isLayerListFull() const noexcept
    {
        return numLayers == Bindings::maxLayers;
    }

    /**
     * Test if the length of the linked list of layers is greater than 1.
     */
    bool isLayered() const noexcept
    {
        return numLayers > 1;
    }

    const_iterator begin() const
    {
        return const_iterator(*this);
    }

    const_iterator end() const
    {
        return const_iterator();
    }

    Attr & operator[](size_type pos)
    {
        if (isLayered()) [[unlikely]]
            unreachable();
        return attrs[pos];
    }

    const Attr & operator[](size_type pos) const
    {
        if (isLayered()) [[unlikely]]
            unreachable();
        return attrs[pos];
    }

    void sort();

    /**
     * Returns the attributes in lexicographically sorted order.
     */
    std::vector<const Attr *> lexicographicOrder(const SymbolTable & symbols) const
    {
        std::vector<const Attr *> res;
        res.reserve(size());
        std::ranges::transform(*this, std::back_inserter(res), [](const Attr & a) { return &a; });
        std::ranges::sort(res, [&](const Attr * a, const Attr * b) {
            std::string_view sa = symbols[a->name], sb = symbols[b->name];
            return sa < sb;
        });
        return res;
    }

    friend class EvalMemory;
};

static_assert(std::forward_iterator<Bindings::iterator>);
static_assert(std::ranges::forward_range<Bindings>);

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
    using size_type = Bindings::size_type;

private:
    Bindings * bindings;
    Bindings::size_type capacity_;

    friend class EvalMemory;

    BindingsBuilder(EvalMemory & mem, SymbolTable & symbols, Bindings * bindings, size_type capacity)
        : bindings(bindings)
        , capacity_(capacity)
        , mem(mem)
        , symbols(symbols)
    {
    }

    bool hasBaseLayer() const noexcept
    {
        return bindings->baseLayer;
    }

    /**
     * If the bindings gets "layered" on top of another we need to recalculate
     * the number of unique attributes in the chain.
     *
     * This is done by either iterating over the base "layer" and the newly added
     * attributes and counting duplicates. If the base "layer" is big this approach
     * is inefficient and we fall back to doing per-element binary search in the base
     * "layer".
     */
    void finishSizeIfNecessary()
    {
        if (!hasBaseLayer())
            return;

        auto & base = *bindings->baseLayer;
        auto attrs = std::span(bindings->attrs, bindings->numAttrs);

        Bindings::size_type duplicates = 0;

        /* If the base bindings is smaller than the newly added attributes
           iterate using std::set_intersection to run in O(|base| + |attrs|) =
           O(|attrs|). Otherwise use an O(|attrs| * log(|base|)) per-attr binary
           search to check for duplicates. Note that if we are in this code path then
           |attrs| <= bindingsUpdateLayerRhsSizeThreshold, which 16 by default. We are
           optimizing for the case when a small attribute set gets "layered" on top of
           a much larger one. When attrsets are already small it's fine to do a linear
           scan, but we should avoid expensive iterations over large "base" attrsets. */
        if (attrs.size() > base.size()) {
            std::set_intersection(
                base.begin(),
                base.end(),
                attrs.begin(),
                attrs.end(),
                boost::make_function_output_iterator([&]([[maybe_unused]] auto && _) { ++duplicates; }));
        } else {
            for (const auto & attr : attrs) {
                if (base.get(attr.name))
                    ++duplicates;
            }
        }

        bindings->numAttrsInChain = base.numAttrsInChain + attrs.size() - duplicates;
    }

public:
    std::reference_wrapper<EvalMemory> mem;
    std::reference_wrapper<SymbolTable> symbols;

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
        assert(bindings->numAttrs < capacity_);
        bindings->push_back(attr);
    }

    /**
     * "Layer" the newly constructured Bindings on top of another attribute set.
     *
     * This effectively performs an attribute set merge, while giving preference
     * to attributes from the newly constructed Bindings in case of duplicate attribute
     * names.
     *
     * This operation amortizes the need to copy over all attributes and allows
     * for efficient implementation of attribute set merges (ExprOpUpdate::eval).
     */
    void layerOnTopOf(const Bindings & base) noexcept
    {
        bindings->baseLayer = &base;
        bindings->numLayers = base.numLayers + 1;
    }

    Value & alloc(Symbol name, PosIdx pos = noPos);

    Value & alloc(std::string_view name, PosIdx pos = noPos);

    Bindings * finish()
    {
        bindings->sort();
        finishSizeIfNecessary();
        return bindings;
    }

    Bindings * alreadySorted()
    {
        finishSizeIfNecessary();
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
