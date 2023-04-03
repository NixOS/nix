#pragma once
///@file

#include "fetchers.hh"
#include "path.hh"

namespace nix::fetchers {

struct Cache
{
    virtual ~Cache() { }

    virtual void add(
        ref<Store> store,
        const Attrs & inAttrs,
        const Attrs & infoAttrs,
        const StorePath & storePath,
        bool locked) = 0;

    virtual std::optional<std::pair<Attrs, StorePath>> lookup(
        ref<Store> store,
        const Attrs & inAttrs) = 0;

    struct Result
    {
        bool expired = false;
        Attrs infoAttrs;
        StorePath storePath;
    };

    virtual std::optional<Result> lookupExpired(
        ref<Store> store,
        const Attrs & inAttrs) = 0;

    /* A simple key/value store for immutable facts such as the
       revcount corresponding to a rev. */
    virtual void upsertFact(
        std::string_view key,
        std::string_view value) = 0;

    virtual std::optional<std::string> queryFact(std::string_view key) = 0;
};

ref<Cache> getCache();

}
