#include <gtest/gtest.h>

#include "nix/store/binary-cache-store.hh"
#include "nix/store/globals.hh"

namespace nix {

struct TestBinaryCacheStoreConfig : virtual Store::Config, BinaryCacheStoreConfig
{
    using Params = StoreReference::Params;

    TestBinaryCacheStoreConfig(const Params & params)
        : Store::Config(params)
        , BinaryCacheStoreConfig(params)
    {
    }

    ref<Store> openStore() const override
    {
        throw Unsupported("openStore");
    }
};

struct TestBinaryCacheStore : virtual BinaryCacheStore
{
    using Config = TestBinaryCacheStoreConfig;

    ref<Config> config;
    std::atomic<size_t> fileExistsCalls = 0;
    std::atomic<size_t> getFileCalls = 0;
    std::set<std::string> existingFiles;

    TestBinaryCacheStore(ref<Config> config)
        : Store{*config}
        , BinaryCacheStore{*config}
        , config(config)
    {
    }

    std::optional<TrustedFlag> isTrustedClient() override
    {
        return std::nullopt;
    }

    bool fileExists(const std::string & path) override
    {
        fileExistsCalls++;
        return existingFiles.contains(path);
    }

    void upsertFile(
        const std::string & path, RestartableSource & source, const std::string & mimeType, uint64_t sizeHint) override
    {
        throw Unsupported("upsertFile");
    }

    void getFile(const std::string & path, Sink & sink) override
    {
        getFileCalls++;
        throw NoSuchBinaryCacheFile("file '%s' does not exist in binary cache", path);
    }
};

TEST(BinaryCacheStore, queryValidPathsUsesExistenceChecks)
{
    initLibStore(false);

    auto config = make_ref<TestBinaryCacheStoreConfig>(StoreReference::Params{});
    auto store = std::make_shared<TestBinaryCacheStore>(config);

    StorePathSet paths{
        StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo"},
        StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3r-bar"},
        StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3s-baz"},
    };

    store->existingFiles = {
        "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q.narinfo",
        "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3s.narinfo",
    };

    auto valid = store->queryValidPaths(paths);

    EXPECT_EQ(valid.size(), 2u);
    EXPECT_EQ(valid.count(StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo"}), 1u);
    EXPECT_EQ(valid.count(StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3s-baz"}), 1u);

    EXPECT_EQ(store->fileExistsCalls, paths.size());
    EXPECT_EQ(store->getFileCalls, 0u);
}

} // namespace nix
