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
        const Attrs & inAttrs,
        const Attrs & infoAttrs) = 0;

    /**
     * Look up a key with infinite TTL.
     */
    virtual std::optional<Attrs> lookup(
        const Attrs & inAttrs) = 0;

    /**
     * Look up a key. Return nothing if its TTL has exceeded
     * `settings.tarballTTL`.
     */
    virtual std::optional<Attrs> lookupWithTTL(
        const Attrs & inAttrs) = 0;

    struct Result2
    {
        bool expired = false;
        Attrs infoAttrs;
    };

    /**
     * Look up a key. Return a bool denoting whether its TTL has
     * exceeded `settings.tarballTTL`.
     */
    virtual std::optional<Result2> lookupExpired(
        const Attrs & inAttrs) = 0;

    /* Old cache for things that have a store path. */
    virtual void add(
        Store & store,
        const Attrs & inAttrs,
        const Attrs & infoAttrs,
        const StorePath & storePath,
        bool locked) = 0;

    virtual std::optional<std::pair<Attrs, StorePath>> lookup(
        Store & store,
        const Attrs & inAttrs) = 0;

    struct Result
    {
        bool expired = false;
        Attrs infoAttrs;
        StorePath storePath;
    };

    virtual std::optional<Result> lookupExpired(
        Store & store,
        const Attrs & inAttrs) = 0;
};

ref<Cache> getCache();

}
