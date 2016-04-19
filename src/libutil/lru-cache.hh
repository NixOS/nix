#pragma once

#include <map>
#include <list>

namespace nix {

/* A simple least-recently used cache. Not thread-safe. */
template<typename Key, typename Value>
class LRUCache
{
private:

    size_t maxSize;

    // Stupid wrapper to get around circular dependency between Data
    // and LRU.
    struct LRUIterator;

    using Data = std::map<Key, std::pair<LRUIterator, Value>>;
    using LRU = std::list<typename Data::iterator>;

    struct LRUIterator { typename LRU::iterator it; };

    Data data;
    LRU lru;

public:

    LRUCache(size_t maxSize) : maxSize(maxSize) { }

    /* Insert or upsert an item in the cache. */
    void upsert(const Key & key, const Value & value)
    {
        erase(key);

        if (data.size() >= maxSize) {
            /* Retire the oldest item. */
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

    bool erase(const Key & key)
    {
        auto i = data.find(key);
        if (i == data.end()) return false;
        lru.erase(i->second.first.it);
        data.erase(i);
        return true;
    }

    /* Look up an item in the cache. If it exists, it becomes the most
       recently used item. */
    // FIXME: use boost::optional?
    Value * get(const Key & key)
    {
        auto i = data.find(key);
        if (i == data.end()) return 0;

        /* Move this item to the back of the LRU list. */
        lru.erase(i->second.first.it);
        auto j = lru.insert(lru.end(), i);
        i->second.first.it = j;

        return &i->second.second;
    }

    size_t size()
    {
        return data.size();
    }

    void clear()
    {
        data.clear();
        lru.clear();
    }
};

}
