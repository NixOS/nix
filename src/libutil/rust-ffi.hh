#pragma once

#include "serialise.hh"

#include <string_view>
#include <cstring>
#include <array>

namespace rust {

typedef void (*DropFun)(void *);

/* A Rust value of N bytes. It can be moved but not copied. When it
   goes out of scope, the C++ destructor will run the drop
   function. */
template<std::size_t N, DropFun drop>
struct Value
{
protected:

    std::array<char, N> raw;

    ~Value()
    {
        if (!isEvacuated()) {
            drop(this);
            evacuate();
        }
    }

    // Must not be called directly.
    Value()
    { }

    Value(Value && other)
        : raw(other.raw)
    {
        other.evacuate();
    }

    void operator =(Value && other)
    {
        if (!isEvacuated())
            drop(this);
        raw = other.raw;
        other.evacuate();
    }

private:

    /* FIXME: optimize these (ideally in such a way that the compiler
       can elide most calls to evacuate() / isEvacuated(). */
    inline void evacuate()
    {
        for (auto & i : raw) i = 0;
    }

    inline bool isEvacuated()
    {
        for (auto & i : raw)
            if (i != 0) return false;
        return true;
    }
};

/* A Rust vector. */
template<typename T, DropFun drop>
struct Vec : Value<3 * sizeof(void *), drop>
{
    inline size_t size() const
    {
        return ((const size_t *) &this->raw)[2];
    }

    const T * data() const
    {
        return ((const T * *) &this->raw)[0];
    }
};

/* A Rust slice. */
template<typename T>
struct Slice
{
    const T * ptr;
    size_t size;

    Slice(const T * ptr, size_t size) : ptr(ptr), size(size)
    {
        assert(ptr);
    }
};

struct StringSlice : Slice<char>
{
    StringSlice(const std::string & s): Slice(s.data(), s.size()) {}
    explicit StringSlice(std::string_view s): Slice(s.data(), s.size()) {}
    StringSlice(const char * s): Slice(s, strlen(s)) {}

    operator std::string_view() const
    {
        return std::string_view(ptr, size);
    }
};

/* A Rust string. */
struct String;

extern "C" {
    void ffi_String_new(StringSlice s, String * out);
    void ffi_String_drop(void * s);
}

struct String : Vec<char, ffi_String_drop>
{
    String(std::string_view s)
    {
        ffi_String_new(StringSlice(s), this);
    }

    String(const char * s)
        : String({s, std::strlen(s)})
    {
    }

    operator std::string_view() const
    {
        return std::string_view(data(), size());
    }
};

std::ostream & operator << (std::ostream & str, const String & s);

struct Source
{
    size_t (*fun)(void * source_this, rust::Slice<uint8_t> data);
    nix::Source * _this;

    Source(nix::Source & _this)
        : fun(sourceWrapper), _this(&_this)
    {}

    // FIXME: how to propagate exceptions?
    static size_t sourceWrapper(void * _this, rust::Slice<uint8_t> data)
    {
        auto n = ((nix::Source *) _this)->read((unsigned char *) data.ptr, data.size);
        return n;
    }
};

/* C++ representation of Rust's Result<T, CppException>. */
template<typename T>
struct Result
{
    unsigned int tag;

    union {
        T data;
        std::exception_ptr * exc;
    };

    ~Result()
    {
        if (tag == 0)
            data.~T();
        else if (tag == 1)
            // FIXME: don't leak exc
            ;
    }

    /* Rethrow the wrapped exception or return the wrapped value. */
    T unwrap()
    {
        if (tag == 0)
            return std::move(data);
        else if (tag == 1)
            std::rethrow_exception(*exc);
        else
            abort();
    }
};

template<typename T>
struct CBox
{
    T * ptr;

    T * operator ->()
    {
        return ptr;
    }

    CBox(T * ptr) : ptr(ptr) { }
    CBox(const CBox &) = delete;
    CBox(CBox &&) = delete;

    ~CBox()
    {
        free(ptr);
    }
};

}
