#pragma once
///@file

#include "nix/util/url.hh"
#include "nix/store/binary-cache-store.hh"

namespace nix {

template<template<typename> class F>
struct HttpBinaryCacheStoreConfigT
{
    F<std::string>::type narinfoCompression;
    F<std::string>::type lsCompression;
    F<std::string>::type logCompression;
};

struct HttpBinaryCacheStoreConfig : std::enable_shared_from_this<HttpBinaryCacheStoreConfig>,
                                    Store::Config,
                                    BinaryCacheStoreConfig,
                                    HttpBinaryCacheStoreConfigT<config::PlainValue>
{
    static config::SettingDescriptionMap descriptions();

    HttpBinaryCacheStoreConfig(std::string_view scheme, std::string_view cacheUri, const StoreConfig::Params & params);

    ParsedURL cacheUri;

    static const std::string name()
    {
        return "HTTP Binary Cache Store";
    }

    static StringSet uriSchemes();

    static std::string doc();

    ref<Store> openStore() const override;

    StoreReference getReference() const override;
};

} // namespace nix
