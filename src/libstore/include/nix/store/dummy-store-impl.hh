#pragma once
///@file

#include "nix/store/dummy-store.hh"

#include <boost/unordered/concurrent_flat_map.hpp>

namespace nix {

struct MemorySourceAccessor;

/**
 * Enough of the Dummy Store exposed for sake of writing unit tests
 */
struct DummyStore : virtual Store
{
    using Config = DummyStoreConfig;

    ref<const Config> config;

    struct PathInfoAndContents
    {
        UnkeyedValidPathInfo info;
        ref<MemorySourceAccessor> contents;
    };

    /**
     * This is map conceptually owns the file system objects for each
     * store object.
     */
    boost::concurrent_flat_map<StorePath, PathInfoAndContents> contents;

    DummyStore(ref<const Config> config)
        : Store{*config}
        , config(config)
    {
    }
};

} // namespace nix
