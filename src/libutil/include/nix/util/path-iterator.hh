#pragma once
///@file

#include "nix/util/unresolved-path.hh"
#include "nix/util/canon-path.hh"

#include <iterator>
#include <ranges>
#include <string_view>
#include <type_traits>

namespace nix {

/**
 * Iterates over the components of an `UnresolvedPath` or `CanonPath`.
 *
 * In its own header, which includes theirs, so that only code that
 * iterates over paths needs it: `UnresolvedPath::begin` and the like are
 * only declared with their classes, and defined here.
 */
template<typename Component>
class BasicPathIterator
{
    /**
     * Helper class with overloaded operator-> for "drill-down" behavior.
     * This was a "temporary" string_view doesn't have to be stored anywhere.
     */
    class PointerProxy
    {
        Component segment;

    public:
        PointerProxy(Component segment_)
            : segment(segment_)
        {
        }

        const Component * operator->() const
        {
            return &segment;
        }
    };

public:
    using value_type = Component;
    using reference_type = const Component;
    using pointer_type = PointerProxy;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;

    std::string_view remaining;
    size_t slash;

    /**
     * Dummy default constructor required for forward iterators. Doesn't return
     * a usable iterator.
     */
    BasicPathIterator()
        : remaining()
        , slash(0)
    {
    }

    BasicPathIterator(std::string_view remaining)
        : remaining(remaining)
        , slash(remaining.find('/'))
    {
    }

    bool operator==(const BasicPathIterator & x) const
    {
        return remaining.data() == x.remaining.data();
    }

    reference_type operator*() const
    {
        auto component = remaining.substr(0, slash);
        if constexpr (std::is_same_v<Component, std::string_view>)
            return component;
        else
            /* A `CanonPath` component is always a valid file name, so
               `Component` may be `FilenameView` for one. */
            return Component::fromStringUnchecked(component);
    }

    pointer_type operator->() const
    {
        return PointerProxy(**this);
    }

    BasicPathIterator & operator++()
    {
        if (slash == remaining.npos)
            remaining = remaining.substr(remaining.size());
        else {
            remaining = remaining.substr(slash + 1);
            slash = remaining.find('/');
        }
        return *this;
    }

    BasicPathIterator operator++(int)
    {
        auto tmp = *this;
        ++*this;
        return tmp;
    }
};

static_assert(std::forward_iterator<BasicPathIterator<std::string_view>>);
static_assert(std::forward_iterator<BasicPathIterator<FilenameView>>);

inline UnresolvedPath::Iterator UnresolvedPath::begin() const
{
    return Iterator(rel());
}

inline UnresolvedPath::Iterator UnresolvedPath::end() const
{
    return Iterator(rel().substr(path.size() - 1));
}

static_assert(std::ranges::forward_range<UnresolvedPath>);

inline CanonPath::Iterator CanonPath::begin() const
{
    return Iterator(rel());
}

inline CanonPath::Iterator CanonPath::end() const
{
    return Iterator(rel().substr(abs().size() - 1));
}

static_assert(std::ranges::forward_range<CanonPath>);

} // namespace nix
