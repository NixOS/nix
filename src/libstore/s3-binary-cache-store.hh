#pragma once
///@file

#include "binary-cache-store.hh"

#include <atomic>

namespace nix {

template<template<typename> class F>
struct S3BinaryCacheStoreConfigT
{
    F<std::string> profile;
    F<std::string> region;
    F<std::string> scheme;
    F<std::string> endpoint;
    F<std::string> narinfoCompression;
    F<std::string> lsCompression;
    F<std::string> logCompression;
    F<bool> multipartUpload;
    F<uint64_t> bufferSize;
};

struct S3BinaryCacheStoreConfig : std::enable_shared_from_this<S3BinaryCacheStoreConfig>,
                                  Store::Config,
                                  BinaryCacheStoreConfig,
                                  S3BinaryCacheStoreConfigT<config::JustValue>
{
    static config::SettingDescriptionMap descriptions();

    S3BinaryCacheStoreConfig(
        std::string_view uriScheme, std::string_view bucketName, const StoreReference::Params & params);

    std::string bucketName;

    static std::string name()
    {
        return "S3 Binary Cache Store";
    }

    static std::set<std::string> uriSchemes()
    {
        return {"s3"};
    }

    static std::string doc();

    ref<Store> openStore() const override;
};

struct S3BinaryCacheStore : virtual BinaryCacheStore
{
    using Config = S3BinaryCacheStoreConfig;

    ref<const Config> config;

    S3BinaryCacheStore(ref<const Config>);

    struct Stats
    {
        std::atomic<uint64_t> put{0};
        std::atomic<uint64_t> putBytes{0};
        std::atomic<uint64_t> putTimeMs{0};
        std::atomic<uint64_t> get{0};
        std::atomic<uint64_t> getBytes{0};
        std::atomic<uint64_t> getTimeMs{0};
        std::atomic<uint64_t> head{0};
    };

    virtual const Stats & getS3Stats() = 0;
};

}
