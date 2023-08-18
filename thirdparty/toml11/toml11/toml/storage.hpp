//     Copyright Toru Niina 2017.
// Distributed under the MIT License.
#ifndef TOML11_STORAGE_HPP
#define TOML11_STORAGE_HPP
#include "utility.hpp"

namespace toml
{
namespace detail
{

// this contains pointer and deep-copy the content if copied.
// to avoid recursive pointer.
template<typename T>
struct storage
{
    using value_type = T;

    explicit storage(value_type const& v): ptr(toml::make_unique<T>(v)) {}
    explicit storage(value_type&&      v): ptr(toml::make_unique<T>(std::move(v))) {}
    ~storage() = default;
    storage(const storage& rhs): ptr(toml::make_unique<T>(*rhs.ptr)) {}
    storage& operator=(const storage& rhs)
    {
        this->ptr = toml::make_unique<T>(*rhs.ptr);
        return *this;
    }
    storage(storage&&) = default;
    storage& operator=(storage&&) = default;

    bool is_ok() const noexcept {return static_cast<bool>(ptr);}

    value_type&       value() &      noexcept {return *ptr;}
    value_type const& value() const& noexcept {return *ptr;}
    value_type&&      value() &&     noexcept {return std::move(*ptr);}

  private:
    std::unique_ptr<value_type> ptr;
};

} // detail
} // toml
#endif// TOML11_STORAGE_HPP
