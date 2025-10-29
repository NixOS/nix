#pragma once
///@file

#include <cassert>
#include <map>
#include <list>
#include <optional>

namespace nix {

/**
 * A simple least-recently used cache. Not thread-safe.
 */
template<typename Key, typename Value, typename Compare = std::less<>>
class LRUCache
{
private:

    size_t capacity;

    // Stupid wrapper to get around circular dependency between Data
    // and LRU.
    struct LRUIterator;

    using Data = std::map<Key, std::pair<LRUIterator, Value>, Compare>;
    using LRU = std::list<typename Data::iterator>;

    struct LRUIterator
    {
        typename LRU::iterator it;
    };

    Data data;
    LRU lru;

    /**
     * Move this item to the back of the LRU list.
     */
    void promote(LRU::iterator it)
    {
        /* Think of std::list iterators as stable pointers to the list node,
         * which never get invalidated. Thus, we can reuse the same lru list
         * element and just splice it to the back of the list without the need
         * to update its value in the key -> list iterator map. */
        lru.splice(/*pos=*/lru.end(), /*other=*/lru, it);
    }

public:

    LRUCache(size_t capacity)
        : capacity(capacity)
    {
    }

    /**
     * Insert or upsert an item in the cache.
     */
    template<typename K>
    void upsert(const K & key, const Value & value)
    {
        if (capacity == 0)
            return;

        erase(key);

        if (data.size() >= capacity) {
            /**
             * Retire the oldest item.
             */
            auto oldest = lru.begin();
            data.erase(*oldest);
            lru.erase(oldest);
        }

        auto res = data.emplace(key, std::make_pair(LRUIterator(), value));
        assert(res.second);
        auto & i(res.first);

        auto j = lru.insert(lru.end(), i);

        i->second.first.it = j;
    }

    template<typename K>
    bool erase(const K & key)
    {
        auto i = data.find(key);
        if (i == data.end())
            return false;
        lru.erase(i->second.first.it);
        data.erase(i);
        return true;
    }

    /**
     * Look up an item in the cache. If it exists, it becomes the most
     * recently used item.
     *
     * @returns corresponding cache entry, std::nullopt if it's not in the cache
     */
    template<typename K>
    std::optional<Value> get(const K & key)
    {
        auto i = data.find(key);
        if (i == data.end())
            return {};

        auto & [it, value] = i->second;
        promote(it.it);
        return value;
    }

    /**
     * Look up an item in the cache. If it exists, it becomes the most
     * recently used item.
     *
     * @returns mutable pointer to the corresponding cache entry, nullptr if
     * it's not in the cache
     */
    template<typename K>
    Value * getOrNullptr(const K & key)
    {
        auto i = data.find(key);
        if (i == data.end())
            return nullptr;

        auto & [it, value] = i->second;
        promote(it.it);
        return &value;
    }

    size_t size() const noexcept
    {
        return data.size();
    }

    void clear() noexcept
    {
        data.clear();
        lru.clear();
    }
};

} // namespace nix
