#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <httplib.h>

#include "nix/store/http-binary-cache-store.hh"
#include "nix/store/path.hh"
#include "nix/util/url.hh"

namespace nix {

TEST(HttpBinaryCacheStore, constructConfig)
{
    HttpBinaryCacheStoreConfig config{"http", "foo.bar.baz", {}};

    EXPECT_EQ(config.cacheUri.to_string(), "http://foo.bar.baz");
}

TEST(HttpBinaryCacheStore, constructConfigNoTrailingSlash)
{
    HttpBinaryCacheStoreConfig config{"https", "foo.bar.baz/a/b/", {}};

    EXPECT_EQ(config.cacheUri.to_string(), "https://foo.bar.baz/a/b");
}

TEST(HttpBinaryCacheStore, constructConfigWithParams)
{
    StoreConfig::Params params{{"compression", "xz"}};
    HttpBinaryCacheStoreConfig config{"https", "foo.bar.baz/a/b/", params};
    EXPECT_EQ(config.cacheUri.to_string(), "https://foo.bar.baz/a/b");
    EXPECT_EQ(config.getReference().params, params);
}

TEST(HttpBinaryCacheStore, constructConfigWithParamsAndUrlWithParams)
{
    StoreConfig::Params params{{"compression", "xz"}};
    HttpBinaryCacheStoreConfig config{"https", "foo.bar.baz/a/b?some-param=some-value", params};
    EXPECT_EQ(config.cacheUri.to_string(), "https://foo.bar.baz/a/b?some-param=some-value");
    EXPECT_EQ(config.getReference().params, params);
}

struct ServerManager
{
    httplib::Server & server;
    std::thread & server_thread;

    ~ServerManager()
    {
        server.stop();
        if (server_thread.joinable()) {
            server_thread.join();
        }
    }
};

TEST(HttpBinaryCacheStore, NixCacheInfoBasicTest)
{
    httplib::Server server;
    server.Get("/nix-cache-info", [](const httplib::Request &, httplib::Response & res) {
        res.set_content(
            "StoreDir: /nix/store\n"
            "WantMassQuery: 1\n"
            "Priority: 40\n",
            "text/plain");
    });
    int port = server.bind_to_any_port("localhost");
    ASSERT_GT(port, 0);

    // MacOS Github actions don't seem to have 127.0.0.1
    auto serverUrl = "localhost:" + std::to_string(port);
    auto serverThread = std::thread([&server]() { server.listen_after_bind(); });
    // Create the RAII guard object. Its destructor will handle cleanup.
    ServerManager manager(server, serverThread);

    auto config = std::make_shared<HttpBinaryCacheStoreConfig>("http", serverUrl, StoreConfig::Params{});
    auto store = config->openStore().dynamic_pointer_cast<BinaryCacheStore>();
    ASSERT_TRUE(store);

    auto cacheInfo = store->getNixCacheInfo();
    EXPECT_EQ(cacheInfo.value(), "StoreDir: /nix/store\nWantMassQuery: 1\nPriority: 40\n");
}

TEST(HttpBinaryCacheStore, UrlEncodingPlusSign)
{
    httplib::Server server;
    const std::string drvNameWithPlus = "bqlpc40ak1qn45zmv44h8cqjx12hphzi-hello+plus-1.0.drv";
    const std::string expectedLogPath = "/log/" + drvNameWithPlus;
    server.Get(R"(/log/(.+))", [&](const httplib::Request & req, httplib::Response & res) {
        if (req.path == expectedLogPath) {
            res.set_content("correct", "text/plain");
            res.status = 200;
        } else {
            res.status = 404;
            res.set_content("Not Found: Path was decoded incorrectly.", "text/plain");
        }
    });
    int port = server.bind_to_any_port("localhost");
    ASSERT_GT(port, 0);

    // MacOS Github actions don't seem to have 127.0.0.1
    auto serverUrl = "localhost:" + std::to_string(port);
    auto serverThread = std::thread([&server]() { server.listen_after_bind(); });
    // Create the RAII guard object. Its destructor will handle cleanup.
    ServerManager manager(server, serverThread);

    auto config = std::make_shared<HttpBinaryCacheStoreConfig>("http", serverUrl, StoreConfig::Params{});
    auto store = config->openStore().dynamic_pointer_cast<BinaryCacheStore>();
    ASSERT_TRUE(store);

    auto buildLog = store->getBuildLogExact(StorePath{drvNameWithPlus});
    EXPECT_THAT(buildLog, testing::Optional(testing::Eq("correct")));
}

} // namespace nix
