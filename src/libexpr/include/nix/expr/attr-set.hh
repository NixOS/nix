#pragma once
///@file

#include "nix/expr/nixexpr.hh"
#include "nix/expr/symbol-table.hh"
#include "nix/util/config.hh"

#include <boost/container/static_vector.hpp>
#include <boost/iterator/function_output_iterator.hpp>

#include <algorithm>
#include <atomic>
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
        , value(value)
    {
    }

    constexpr Attr() {}

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
    static const constinit Bindings emptyBindings;

private:
    /**
     * Number of attributes in the attrs FAM (Flexible Array Member).
     */
    size_type numAttrs = 0;

    /**
     * Number of attributes with unique names in the layer chain -- the
     * *real* size, whereas @ref numAttrs is just this layer's own count.
     *
     * Only meaningful for a *layered* Bindings -- @ref size reads
     * @ref numAttrs directly for a flat one, never touching this field.
     * Starts at 0 ("not yet computed") and @ref size computes and
     * memoizes it lazily on first call: 0 is never the real result of
     * that computation, since layering only happens between two
     * non-empty Bindings, so there's no ambiguity between "not yet
     * computed" and "genuinely empty".
     *
     * Atomic (relaxed) rather than a plain `mutable`: the computed value is
     * deterministic, so concurrent writers racing to compute and store it
     * would always agree on the value, but a plain non-atomic write would
     * still be a data race (undefined behavior) under any future
     * multi-threaded evaluator. Relaxed ordering is enough since no other
     * memory access needs to be ordered against this one -- it's a pure
     * function of already-fixed data, not a synchronization point.
     */
    mutable std::atomic<size_type> numAttrsInChain{0};

    /**
     * Set once construction finishes; catches a lazy @ref size running
     * too early. Free: shares the alignment gap before @ref baseLayer
     * with @ref numLayers below.
     */
    bool finished = false;

    /**
     * Length of the layers list. uint8_t since maxLayers is 16 --
     * see the sizeof(Bindings) static_assert.
     */
    uint8_t numLayers = 1;

    /**
     * Bindings that this attrset is "layered" on top of.
     */
    const Bindings * baseLayer = nullptr;

    /**
     * Flexible array member of attributes.
     */
    Attr attrs[0];

    constexpr Bindings() = default;
    Bindings(const Bindings &) = delete;
    Bindings(Bindings &&) = delete;
    Bindings & operator=(const Bindings &) = delete;
    Bindings & operator=(Bindings &&) = delete;

    ~Bindings() = default;

    friend class BindingsBuilder;

    /**
     * Maximum length of the Bindings layer chains.
     */
    static constexpr unsigned maxLayers = 16;

public:
    /**
     * Number of attributes with unique names in the layer chain -- a flat
     * Bindings' @ref numAttrs is already the exact count, no computation
     * needed; see @ref numAttrsInChain's doc comment for the layered case.
     *
     * If the bindings gets "layered" on top of another we need to
     * recalculate the number of unique attributes in the chain, the
     * first time it's asked for, then memoize it: either iterate over
     * the base "layer" and the newly added attributes with
     * std::set_intersection, O(|base| + |attrs|), or do an O(|attrs| *
     * log(|base|)) per-attr binary search in the base "layer" -- picked
     * per-call based on which is cheaper for the actual sizes, since
     * |attrs| isn't bounded small here the way it is under the absolute
     * threshold alone (relative-size layering lets a RHS bigger than
     * the threshold still layer, as long as it's smaller than the
     * accumulator).
     */
    size_type size() const
    {
        if (!baseLayer)
            return numAttrs;

        auto cached = numAttrsInChain.load(std::memory_order_relaxed);
        if (cached != 0)
            return cached;

        if (!finished) [[unlikely]]
            nixUnreachableWhenHardened();

        auto & base = *baseLayer;
        auto ownAttrs = std::span(attrs, numAttrs);

        size_type duplicates = 0;

        if (ownAttrs.size() > base.size()) {
            std::set_intersection(
                base.begin(),
                base.end(),
                ownAttrs.begin(),
                ownAttrs.end(),
                boost::make_function_output_iterator([&]([[maybe_unused]] auto && _) { ++duplicates; }));
        } else {
            for (const auto & attr : ownAttrs)
                if (base.get(attr.name))
                    ++duplicates;
        }

        size_type result = base.size() + ownAttrs.size() - duplicates;
        numAttrsInChain.store(result, std::memory_order_relaxed);
        return result;
    }

    bool empty() const
    {
        /* A layered chain is never empty (layering only happens between
           two non-empty Bindings), so this avoids forcing size()'s
           computation on every `//` for the long-lived "prev" accumulator. */
        if (baseLayer)
            return false;
        return numAttrs == 0;
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
            std::ranges::push_heap(cursorHeap, comp);
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

/* 24 bytes with an 8-byte baseLayer pointer (64-bit), 20 with a
   4-byte one (32-bit) -- the small fields round up to the same 16
   either way, only the trailing pointer's width differs. */
static_assert(sizeof(Bindings) == 16 + sizeof(void *));

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

    const Bindings * finalize()
    {
        assert(!bindings->baseLayer || (bindings->numAttrs > 0 && !bindings->baseLayer->empty()));
        bindings->finished = true;
        return bindings;
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
        // Reset to the "not computed" sentinel now that it's layered.
        bindings->numAttrsInChain.store(0, std::memory_order_relaxed);
    }

    Value & alloc(Symbol name, PosIdx pos = noPos);

    Value & alloc(std::string_view name, PosIdx pos = noPos);

    const Bindings * finish()
    {
        bindings->sort();
        return finalize();
    }

    const Bindings * alreadySorted()
    {
        return finalize();
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
