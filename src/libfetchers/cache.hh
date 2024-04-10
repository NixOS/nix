#pragma once
///@file

#include "fetchers.hh"
#include "path.hh"

namespace nix::fetchers {

/**
 * A cache for arbitrary `Attrs` -> `Attrs` mappings with a timestamp
 * for expiration.
 */
struct Cache
{
    virtual ~Cache() { }

    /**
     * Add a value to the cache. The cache is an arbitrary mapping of
     * Attrs to Attrs.
     */
    virtual void upsert(
        std::string_view domain,
        const Attrs & key,
        const Attrs & value) = 0;

    /**
     * Look up a key with infinite TTL.
     */
    virtual std::optional<Attrs> lookup(
        std::string_view domain,
        const Attrs & key) = 0;

    /**
     * Look up a key. Return nothing if its TTL has exceeded
     * `settings.tarballTTL`.
     */
    virtual std::optional<Attrs> lookupWithTTL(
        std::string_view domain,
        const Attrs & key) = 0;

    struct Result
    {
        bool expired = false;
        Attrs value;
    };

    /**
     * Look up a key. Return a bool denoting whether its TTL has
     * exceeded `settings.tarballTTL`.
     */
    virtual std::optional<Result> lookupExpired(
        std::string_view domain,
        const Attrs & key) = 0;

    /**
     * Insert a cache entry that has a store path associated with
     * it. Such cache entries are always considered stale if the
     * associated store path is invalid.
     */
    virtual void upsert(
        std::string_view domain,
        Attrs key,
        Store & store,
        Attrs value,
        const StorePath & storePath) = 0;

    struct ResultWithStorePath : Result
    {
        StorePath storePath;
    };

    /**
     * Look up a store path in the cache. The returned store path will
     * be valid, but it may be expired.
     */
    virtual std::optional<ResultWithStorePath> lookupStorePath(
        std::string_view domain,
        Attrs key,
        Store & store) = 0;

    /**
     * Look up a store path in the cache. Return nothing if its TTL
     * has exceeded `settings.tarballTTL`.
     */
    virtual std::optional<ResultWithStorePath> lookupStorePathWithTTL(
        std::string_view domain,
        Attrs key,
        Store & store) = 0;
};

ref<Cache> getCache();

}
