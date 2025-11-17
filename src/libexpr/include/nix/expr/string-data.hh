#pragma once
///@file

#include <cstddef>
#include <cstring>
#include <memory_resource>
#include <string_view>

namespace nix {

class StringData
{
public:
    using size_type = std::size_t;

    size_type size_;
    char data_[];

    /*
     * This in particular ensures that we cannot have a `StringData`
     * that we use by value, which is just what we want!
     *
     * Dynamically sized types aren't a thing in C++ and even flexible array
     * members are a language extension and beyond the realm of standard C++.
     * Technically, sizeof data_ member is 0 and the intended way to use flexible
     * array members is to allocate sizeof(StrindData) + count * sizeof(char) bytes
     * and the compiler will consider alignment restrictions for the FAM.
     *
     */

    StringData(StringData &&) = delete;
    StringData & operator=(StringData &&) = delete;
    StringData(const StringData &) = delete;
    StringData & operator=(const StringData &) = delete;
    ~StringData() = default;

private:
    StringData() = delete;

    explicit StringData(size_type size)
        : size_(size)
    {
    }

public:
    /**
     * Allocate StringData on the (possibly) GC-managed heap and copy
     * the contents of s to it.
     */
    static const StringData & make(std::string_view s);

    /**
     * Allocate StringData on the (possibly) GC-managed heap.
     * @param size Length of the string (without the NUL terminator).
     */
    static StringData & alloc(size_t size);

    size_t size() const
    {
        return size_;
    }

    char * data() noexcept
    {
        return data_;
    }

    const char * data() const noexcept
    {
        return data_;
    }

    const char * c_str() const noexcept
    {
        return data_;
    }

    constexpr std::string_view view() const noexcept
    {
        return std::string_view(data_, size_);
    }

    template<size_t N>
    struct Static;

    static StringData & make(std::pmr::memory_resource & resource, std::string_view s)
    {
        auto & res =
            *new (resource.allocate(sizeof(StringData) + s.size() + 1, alignof(StringData))) StringData(s.size());
        std::memcpy(res.data_, s.data(), s.size());
        res.data_[s.size()] = '\0';
        return res;
    }
};

} // namespace nix
