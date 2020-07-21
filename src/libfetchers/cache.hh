#pragma once

#include "fetchers.hh"

namespace nix::fetchers {

struct Cache
{
    virtual ~Cache() { }

    virtual void add(
        ref<Store> store,
        const Attrs & inAttrs,
        const Attrs & infoAttrs,
        const StorePathDescriptor & storePath,
        bool immutable) = 0;

    virtual std::optional<std::pair<Attrs, StorePathDescriptor>> lookup(
        ref<Store> store,
        const Attrs & inAttrs) = 0;

    struct Result
    {
        bool expired = false;
        Attrs infoAttrs;
        StorePathDescriptor storePath;
    };

    virtual std::optional<Result> lookupExpired(
        ref<Store> store,
        const Attrs & inAttrs) = 0;
};

ref<Cache> getCache();

}
