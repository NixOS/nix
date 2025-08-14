#pragma once
///@file

#include "nix/store/store-api.hh"

namespace nix {

/**
 * Helper to try downcasting a Store with a nice method if it fails.
 *
 * This is basically an alternative to the user-facing part of
 * Store::unsupported that allows us to still have a nice message but
 * better interface design.
 */
template<typename T>
T & require(Store & store)
{
    auto * castedStore = dynamic_cast<T *>(&store);
    if (!castedStore)
        throw UsageError("%s not supported by store '%s'", T::operationName, store.config.getHumanReadableURI());
    return *castedStore;
}

} // namespace nix
