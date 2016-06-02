#pragma once

#include <memory>
#include <exception>
#include <stdexcept>

namespace nix {

/* A simple non-nullable reference-counted pointer. Actually a wrapper
   around std::shared_ptr that prevents non-null constructions. */
template<typename T>
class ref
{
private:

    std::shared_ptr<T> p;

public:

    ref<T>(const ref<T> & r)
        : p(r.p)
    { }

    explicit ref<T>(const std::shared_ptr<T> & p)
        : p(p)
    {
        if (!p)
            throw std::invalid_argument("null pointer cast to ref");
    }

    explicit ref<T>(T * p)
        : p(p)
    {
        if (!p)
            throw std::invalid_argument("null pointer cast to ref");
    }

    T* operator ->() const
    {
        return &*p;
    }

    T& operator *() const
    {
        return *p;
    }

    operator std::shared_ptr<T> () const
    {
        return p;
    }

    std::shared_ptr<T> get_ptr() const
    {
        return p;
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
    operator ref<T2> () const
    {
        return ref<T2>((std::shared_ptr<T2>) p);
    }

private:

    template<typename T2, typename... Args>
    friend ref<T2>
    make_ref(Args&&... args);

};

template<typename T, typename... Args>
inline ref<T>
make_ref(Args&&... args)
{
    auto p = std::make_shared<T>(std::forward<Args>(args)...);
    return ref<T>(p);
}

}
