#pragma once
///@file

#include "nix/fetchers/fetchers.hh"
#include "nix/store/path.hh"

#include <functional>
#include <string_view>

namespace nix::fetchers {

/**
 * Get the path for a fetch lock file based on an identity string.
 * The identity is hashed to create a unique lock file name in
 * ~/.cache/nix/fetch-locks/.
 */
Path getFetchLockPath(std::string_view identity);

/**
 * Execute a function while holding a fetch lock.
 * Implements double-checked locking with stale lock detection.
 *
 * This helper coordinates between processes to prevent duplicate fetches.
 * It acquires a file lock, checks if the resource is cached, and only
 * performs the fetch if necessary.
 *
 * @tparam CheckCache Callable returning std::optional<T>
 * @tparam DoFetch Callable returning T
 * @param lockIdentity String identifying the resource (used to generate lock path)
 * @param lockTimeout Timeout in seconds (0 = wait indefinitely)
 * @param checkCache Called after acquiring lock (double-check); if returns value, use it
 * @param doFetch Called under lock if checkCache returns nullopt
 * @return Result from checkCache or doFetch
 * @throws Error if lock acquisition times out
 */
template<typename CheckCache, typename DoFetch>
auto withFetchLock(
    std::string_view lockIdentity, unsigned int lockTimeout, CheckCache && checkCache, DoFetch && doFetch)
    -> decltype(doFetch());

/**
 * A cache for arbitrary `Attrs` -> `Attrs` mappings with a timestamp
 * for expiration.
 */
struct Cache
{
    virtual ~Cache() {}

    /**
     * A domain is a partition of the key/value cache for a particular
     * purpose, e.g. git revision to revcount.
     */
    using Domain = std::string_view;

    /**
     * A cache key is a domain and an arbitrary set of attributes.
     */
    using Key = std::pair<Domain, Attrs>;

    /**
     * Add a key/value pair to the cache.
     */
    virtual void upsert(const Key & key, const Attrs & value) = 0;

    /**
     * Look up a key with infinite TTL.
     */
    virtual std::optional<Attrs> lookup(const Key & key) = 0;

    /**
     * Look up a key. Return nothing if its TTL has exceeded
     * `settings.tarballTTL`.
     */
    virtual std::optional<Attrs> lookupWithTTL(const Key & key) = 0;

    struct Result
    {
        bool expired = false;
        Attrs value;
    };

    /**
     * Look up a key. Return a bool denoting whether its TTL has
     * exceeded `settings.tarballTTL`.
     */
    virtual std::optional<Result> lookupExpired(const Key & key) = 0;

    /**
     * Insert a cache entry that has a store path associated with
     * it. Such cache entries are always considered stale if the
     * associated store path is invalid.
     */
    virtual void upsert(Key key, Store & store, Attrs value, const StorePath & storePath) = 0;

    struct ResultWithStorePath : Result
    {
        StorePath storePath;
    };

    /**
     * Look up a store path in the cache. The returned store path will
     * be valid, but it may be expired.
     */
    virtual std::optional<ResultWithStorePath> lookupStorePath(Key key, Store & store) = 0;

    /**
     * Look up a store path in the cache. Return nothing if its TTL
     * has exceeded `settings.tarballTTL`.
     */
    virtual std::optional<ResultWithStorePath> lookupStorePathWithTTL(Key key, Store & store) = 0;

    /**
     * Atomically look up or fetch a store path with inter-process locking.
     *
     * This method ensures that only one process fetches a given resource
     * at a time. Other processes waiting for the same resource will block
     * until the fetch completes, then return the cached result.
     *
     * @param key The cache key to look up or fetch
     * @param store The store to use for path validation
     * @param fetcher A function that performs the actual fetch and returns
     *                (attributes, storePath) to cache
     * @param lockTimeout Timeout in seconds for acquiring the lock (0 = no timeout)
     * @return The cached result, or std::nullopt if the fetch failed
     * @throws Error if lock acquisition times out
     */
    virtual std::optional<ResultWithStorePath> lookupOrFetch(
        Key key, Store & store, std::function<std::pair<Attrs, StorePath>()> fetcher, unsigned int lockTimeout = 0) = 0;
};

} // namespace nix::fetchers
