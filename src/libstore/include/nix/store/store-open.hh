#pragma once
/**
 * @file
 *
 * For opening a store described by an `StoreReference`, which is an "untyped"
 * notion which needs to be decoded against a collection of specific
 * implementations.
 *
 * For consumers of the store registration machinery defined in
 * `store-registration.hh`. Not needed by store implementation definitions, or
 * usages of a given `Store` which will be passed in.
 */

#include "nix/store/store-api.hh"

namespace nix {

/**
 * @return The store config denoted by `storeURI` (slight misnomer...).
 */
ref<StoreConfig> resolveStoreConfig(StoreReference && storeURI);

/**
 * @return a Store object to access the Nix store denoted by
 * ‘uri’ (slight misnomer...).
 */
ref<Store> openStore(StoreReference && storeURI);

/**
 * Opens the store at `uri`, where `uri` is in the format expected by
 * `StoreReference::parse`
 */
ref<Store> openStore(const std::string & uri, const StoreReference::Params & extraParams = StoreReference::Params());

/**
 * Short-hand which opens the default store, according to global settings
 */
ref<Store> openStore();

/**
 * @return the default substituter stores, defined by the
 * ‘substituters’ option and various legacy options.
 */
std::list<ref<Store>> getDefaultSubstituters();

} // namespace nix
