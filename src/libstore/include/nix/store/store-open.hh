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
 *
 * The `SecretContext` overloads hand the opened store the authority of the
 * opening process, so that stores which make requests themselves (the HTTP and
 * S3 binary caches) can resolve configured credentials. Callers that
 * hold no such authority pass an empty context explicitly.
 */
ref<Store> openStore(const SecretContext & context, StoreReference && storeURI);

ref<Store> openStore(StoreReference && storeURI);

/**
 * Opens the store at `uri`, where `uri` is in the format expected by
 * `StoreReference::parse`
 */
ref<Store> openStore(
    const SecretContext & context,
    const std::string & uri,
    const StoreReference::Params & extraParams = StoreReference::Params());

ref<Store> openStore(const std::string & uri, const StoreReference::Params & extraParams = StoreReference::Params());

/**
 * Short-hand which opens the default store, according to global settings
 */
ref<Store> openStore(const SecretContext & context);

ref<Store> openStore();

/**
 * @return the default substituter stores, defined by the
 * ‘substituters’ option and various legacy options.
 *
 * @note The list is cached for the most recently used resolver. A caller with
 * different authority rebuilds it, so substituter stores never inherit the
 * resolver selected by an unrelated caller.
 */
std::list<ref<Store>> getDefaultSubstituters(const SecretContext & context);

std::list<ref<Store>> getDefaultSubstituters();

} // namespace nix
