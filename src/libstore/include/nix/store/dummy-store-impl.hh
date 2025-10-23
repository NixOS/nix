#pragma once
///@file

#include "nix/store/dummy-store.hh"
#include "nix/store/derivations.hh"

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
     * This map conceptually owns the file system objects for each
     * store object.
     */
    boost::concurrent_flat_map<StorePath, PathInfoAndContents> contents;

    /**
     * This map conceptually owns every derivation, allowing us to
     * avoid "on-disk drv format" serialization round-trips.
     */
    boost::concurrent_flat_map<StorePath, Derivation> derivations;

    /**
     * The build trace maps the pair of a content-addressing (fixed or
     * floating) derivations an one of its output to a
     * (content-addressed) store object.
     *
     * It is [curried](https://en.wikipedia.org/wiki/Currying), so we
     * instead having a single output with a `DrvOutput` key, we have an
     * outer map for the derivation, and inner maps for the outputs of a
     * given derivation.
     */
    boost::concurrent_flat_map<Hash, std::map<std::string, ref<UnkeyedRealisation>>> buildTrace;

    DummyStore(ref<const Config> config)
        : Store{*config}
        , config(config)
    {
    }
};

} // namespace nix
