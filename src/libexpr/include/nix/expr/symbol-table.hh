#pragma once
///@file

#include <memory_resource>
#include "nix/expr/value.hh"
#include "nix/util/chunked-vector.hh"
#include "nix/util/error.hh"

#include <boost/version.hpp>
#if BOOST_VERSION >= 108100
#  include <boost/unordered/unordered_flat_set.hpp>
#  define USE_FLAT_SYMBOL_SET
#else
#  include <boost/unordered/unordered_set.hpp>
#endif

namespace nix {

class SymbolValue : protected Value
{
    friend class SymbolStr;
    friend class SymbolTable;

    uint32_t size_;
    uint32_t idx;

    SymbolValue() = default;

public:
    inline size_t size() const
    {
        return size_;
    }

    operator std::string_view() const
    {
        return {payload.string.c_str, size_};
    }
};

/**
 * Symbols have the property that they can be compared efficiently
 * (using an equality test), because the symbol table stores only one
 * copy of each string.
 */
class Symbol
{
    friend class SymbolStr;
    friend class SymbolTable;

private:
    uint32_t id;

    explicit Symbol(uint32_t id): id(id) {}

public:
    Symbol() : id(0) {}

    explicit operator bool() const { return id > 0; }

    auto operator<=>(const Symbol other) const { return id <=> other.id; }
    bool operator==(const Symbol other) const { return id == other.id; }

    friend class std::hash<Symbol>;
};

/**
 * This class mainly exists to give us an operator<< for ostreams. We could also
 * return plain strings from SymbolTable, but then we'd have to wrap every
 * instance of a symbol that is fmt()ed, which is inconvenient and error-prone.
 */
class SymbolStr
{
    friend class SymbolTable;

    constexpr static size_t chunkSize{8192};
    using SymbolValueStore = ChunkedVector<SymbolValue, chunkSize>;

    const SymbolValue * s;

    struct Key
    {
        using HashType = boost::hash<std::string_view>;

        SymbolValueStore & store;
        std::string_view s;
        std::size_t hash;
        std::pmr::polymorphic_allocator<char> & alloc;

        Key(SymbolValueStore & store, std::string_view s, std::pmr::polymorphic_allocator<char> & stringAlloc)
            : store(store)
            , s(s)
            , hash(HashType{}(s))
            , alloc(stringAlloc) {}
    };

public:
    SymbolStr(const SymbolValue & s) : s(&s) {}

    SymbolStr(const Key & key)
    {
        auto size = key.s.size();
        if (size >= std::numeric_limits<uint32_t>::max()) {
            throw Error("Size of symbol exceeds 4GiB and cannot be stored");
        }
        // for multi-threaded implementations: lock store and allocator here
        const auto & [v, idx] = key.store.add(SymbolValue{});
        if (size == 0) {
            v.mkString("", nullptr);
        } else {
            auto s = key.alloc.allocate(size + 1);
            if (!s) throw std::bad_alloc();
            memcpy(s, key.s.data(), size);
            s[size] = '\0';
            v.mkString(s, nullptr);
        }
        v.size_ = size;
        v.idx = idx;
        this->s = &v;
    }

    bool operator == (std::string_view s2) const
    {
        return *s == s2;
    }

    inline const char * c_str() const
    {
        return s->payload.string.c_str;
    }

    inline operator std::string_view () const
    {
        return *s;
    }

    friend std::ostream & operator <<(std::ostream & os, const SymbolStr & symbol);

    inline bool empty() const
    {
        return s->size_ == 0;
    }

    inline size_t size() const
    {
        return s->size_;
    }

    inline Value * value_ptr() const
    {
        return const_cast<SymbolValue *>(s);
    }

    inline explicit operator Symbol() const
    {
        return Symbol{s->idx + 1};
    }

    struct hash
    {
        using is_transparent = void;
        using is_avalanching = std::true_type;

        std::size_t operator()(SymbolStr str) const
        {
            return Key::HashType{}(*str.s);
        }

        std::size_t operator()(const Key & key) const
        {
            return key.hash;
        }
    };

    struct equal
    {
        using is_transparent = void;

        bool operator()(SymbolStr a, SymbolStr b) const
        {
            // strings are unique, so that a pointer comparison is OK
            return a.s == b.s;
        }

        bool operator()(SymbolStr a, const Key & b) const
        {
            return a == b.s;
        }

        inline bool operator()(const Key & a, SymbolStr b) const
        {
            return operator()(b, a);
        }
    };
};

/**
 * Symbol table used by the parser and evaluator to represent and look
 * up identifiers and attributes efficiently.
 */
class SymbolTable
{
private:
    /**
     * SymbolTable is an append only data structure.
     * During its lifetime the monotonic buffer holds all strings and nodes, if the symbol set is node based.
     */
    std::pmr::monotonic_buffer_resource buffer;
    std::pmr::polymorphic_allocator<char> stringAlloc{&buffer};
    SymbolStr::SymbolValueStore store{16};

    /**
     * Transparent lookup of string view for a pointer to a ChunkedVector entry -> return offset into the store.
     * ChunkedVector references are never invalidated.
     */
#ifdef USE_FLAT_SYMBOL_SET
    boost::unordered_flat_set<SymbolStr, SymbolStr::hash, SymbolStr::equal> symbols{SymbolStr::chunkSize};
#else
    using SymbolValueAlloc = std::pmr::polymorphic_allocator<SymbolStr>;
    boost::unordered_set<SymbolStr, SymbolStr::hash, SymbolStr::equal, SymbolValueAlloc> symbols{SymbolStr::chunkSize, {&buffer}};
#endif

public:

    /**
     * Converts a string into a symbol.
     */
    template<typename T = const SymbolStr::Key &>
    Symbol create(std::string_view s)
    {
        // Most symbols are looked up more than once, so we trade off insertion performance
        // for lookup performance.
        // FIXME: make this thread-safe.
        const SymbolStr::Key key(store, s, stringAlloc);
        if constexpr (requires { symbols.insert<T>(key); }) {
            auto [it, _] = symbols.insert<T>(key);
            return Symbol(*it);
        } else {
            auto it = symbols.find<T>(key);
            if (it != symbols.end())
                return Symbol(*it);

            it = symbols.emplace(key).first;
            return Symbol(*it);
        }
    }

    std::vector<SymbolStr> resolve(const std::vector<Symbol> & symbols) const
    {
        std::vector<SymbolStr> result;
        result.reserve(symbols.size());
        for (auto sym : symbols)
            result.push_back((*this)[sym]);
        return result;
    }

    SymbolStr operator[](Symbol s) const
    {
        uint32_t idx = s.id - uint32_t(1);
        if (idx >= store.size())
            unreachable();
        return store[idx];
    }

    size_t size() const
    {
        return store.size();
    }

    size_t totalSize() const;

    template<typename T>
    void dump(T callback) const
    {
        store.forEach(callback);
    }
};

}

template<>
struct std::hash<nix::Symbol>
{
    std::size_t operator()(const nix::Symbol & s) const noexcept
    {
        return std::hash<decltype(s.id)>{}(s.id);
    }
};

#undef USE_FLAT_SYMBOL_SET