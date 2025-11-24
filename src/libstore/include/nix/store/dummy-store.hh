#pragma once
///@file

#include "nix/store/store-api.hh"

#include <boost/unordered/concurrent_flat_map.hpp>

namespace nix {

struct DummyStore;

template<template<typename> class F>
struct DummyStoreConfigT
{
    F<bool>::type readOnly;
};

struct DummyStoreConfig : public std::enable_shared_from_this<DummyStoreConfig>,
                          Store::Config,
                          DummyStoreConfigT<config::PlainValue>
{
    static config::SettingDescriptionMap descriptions();

    DummyStoreConfig(const Params & params);

    DummyStoreConfig(std::string_view scheme, std::string_view authority, const Params & params)
        : DummyStoreConfig(params)
    {
        if (!authority.empty())
            throw UsageError("`%s` store URIs must not contain an authority part %s", scheme, authority);
    }

    static const std::string name()
    {
        return "Dummy Store";
    }

    static std::string doc();

    static StringSet uriSchemes()
    {
        return {"dummy"};
    }

    /**
     * Same as `openStore`, just with a more precise return type.
     */
    ref<DummyStore> openDummyStore() const;

    ref<Store> openStore() const override;

    StoreReference getReference() const override;
};

} // namespace nix
