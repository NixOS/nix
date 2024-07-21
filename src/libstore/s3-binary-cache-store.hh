#pragma once
///@file

#include "binary-cache-store.hh"

#include <atomic>

namespace nix {

template<template<typename> class F>
struct S3BinaryCacheStoreConfigT
{
    const F<std::string> profile;
    const F<std::string> region;
    const F<std::string> scheme;
    const F<std::string> endpoint;
    const F<std::string> narinfoCompression;
    const F<std::string> lsCompression;
    const F<std::string> logCompression;
    const F<bool> multipartUpload;
    const F<uint64_t> bufferSize;
};

struct S3BinaryCacheStoreConfig :
    virtual BinaryCacheStoreConfig,
    S3BinaryCacheStoreConfigT<config::JustValue>
{
    struct Descriptions :
        virtual Store::Config::Descriptions,
        virtual BinaryCacheStore::Config::Descriptions,
        S3BinaryCacheStoreConfigT<config::SettingInfo>
    {
        Descriptions();
    };

    static const Descriptions descriptions;

    static S3BinaryCacheStoreConfigT<config::JustValue> defaults;

    S3BinaryCacheStoreConfig(
        std::string_view uriScheme, std::string_view bucketName, const StoreReference::Params & params);

    std::string bucketName;

    const std::string name() override
    {
        return "S3 Binary Cache Store";
    }

    static std::set<std::string> uriSchemes()
    {
        return {"s3"};
    }

    std::string doc() override;

    ref<Store> openStore() const override;
};

struct S3BinaryCacheStore : virtual BinaryCacheStore
{
    using Config = S3BinaryCacheStoreConfig;

protected:

    S3BinaryCacheStore(const Config &);

public:

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
