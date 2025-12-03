#pragma once
///@file

#include <memory>
#include <stdexcept>

namespace nix {

/**
 * A simple non-nullable reference-counted pointer. Actually a wrapper
 * around std::shared_ptr that prevents null constructions.
 */
template<typename T>
class ref
{
private:

    std::shared_ptr<T> p;

    void assertNonNull()
    {
        if (!p)
            throw std::invalid_argument("null pointer cast to ref");
    }

public:

    using element_type = T;

    explicit ref(const std::shared_ptr<T> & p)
        : p(p)
    {
        assertNonNull();
    }

    explicit ref(std::shared_ptr<T> && p)
        : p(std::move(p))
    {
        assertNonNull();
    }

    explicit ref(T * p)
        : p(p)
    {
        assertNonNull();
    }

    T * operator->() const
    {
        return &*p;
    }

    T & operator*() const
    {
        return *p;
    }

    std::shared_ptr<T> get_ptr() const &
    {
        return p;
    }

    std::shared_ptr<T> get_ptr() &&
    {
        return std::move(p);
    }

    /**
     * Convenience to avoid explicit `get_ptr()` call in some cases.
     */
    operator std::shared_ptr<T>(this auto && self)
    {
        return std::forward<decltype(self)>(self).get_ptr();
    }

    template<typename T2>
    ref<T2> cast() const
    {
        return ref<T2>(std::dynamic_pointer_cast<T2>(p));
    }

    template<typename T2>
    std::shared_ptr<T2> dynamic_pointer_cast() const
    {
        return std::dynamic_pointer_cast<T2>(p);
    }

    template<typename T2>
    operator ref<T2>() const
    {
        return ref<T2>((std::shared_ptr<T2>) p);
    }

    bool operator==(const ref<T> & other) const
    {
        return p == other.p;
    }

    bool operator!=(const ref<T> & other) const
    {
        return p != other.p;
    }

    auto operator<=>(const ref<T> & other) const
    {
        return p <=> other.p;
    }

private:

    template<typename T2, typename... Args>
    friend ref<T2> make_ref(Args &&... args);
};

template<typename T, typename... Args>
inline ref<T> make_ref(Args &&... args)
{
    auto p = std::make_shared<T>(std::forward<Args>(args)...);
    return ref<T>(p);
}

} // namespace nix
