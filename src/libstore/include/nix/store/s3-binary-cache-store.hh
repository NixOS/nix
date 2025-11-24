#pragma once
///@file

#include "nix/store/config.hh"
#include "nix/store/http-binary-cache-store.hh"

namespace nix {

template<template<typename> class F>
struct S3BinaryCacheStoreConfigT
{
    F<std::string>::type profile;
    F<std::string>::type region;
    F<std::string>::type scheme;
    F<std::string>::type endpoint;
    F<bool>::type multipartUpload;
    F<uint64_t>::type multipartChunkSize;
    F<uint64_t>::type multipartThreshold;
    F<std::optional<std::string>>::type storageClass;
};

struct S3BinaryCacheStoreConfig : std::enable_shared_from_this<S3BinaryCacheStoreConfig>,
                                  HttpBinaryCacheStoreConfig,
                                  S3BinaryCacheStoreConfigT<config::PlainValue>
{
    static config::SettingDescriptionMap descriptions();

    S3BinaryCacheStoreConfig(
        std::string_view uriScheme, std::string_view bucketName, const StoreConfig::Params & params);

    static const std::string name()
    {
        return "S3 Binary Cache Store";
    }

    static StringSet uriSchemes();

    static std::string doc();

    std::string getHumanReadableURI() const override;

    ref<Store> openStore() const override;
};

} // namespace nix
