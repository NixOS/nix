#pragma once
///@file

#include "nix/util/url.hh"
#include "nix/store/binary-cache-store.hh"
#include "nix/store/filetransfer.hh"
#include "nix/util/sync.hh"

#include <chrono>

namespace nix {

struct HttpBinaryCacheStoreConfig : std::enable_shared_from_this<HttpBinaryCacheStoreConfig>,
                                    virtual Store::Config,
                                    BinaryCacheStoreConfig
{
    using BinaryCacheStoreConfig::BinaryCacheStoreConfig;

    HttpBinaryCacheStoreConfig(
        std::string_view scheme, std::string_view cacheUri, const Store::Config::Params & params);

    ParsedURL cacheUri;

    const Setting<std::string> narinfoCompression{
        this, "", "narinfo-compression", "Compression method for `.narinfo` files."};

    const Setting<std::string> lsCompression{this, "", "ls-compression", "Compression method for `.ls` files."};

    const Setting<std::string> logCompression{
        this,
        "",
        "log-compression",
        R"(
          Compression method for `log/*` files. It is recommended to
          use a compression method supported by most web browsers
          (e.g. `brotli`).
        )"};

    static const std::string name()
    {
        return "HTTP Binary Cache Store";
    }

    static StringSet uriSchemes();

    static std::string doc();

    ref<Store> openStore() const override;

    StoreReference getReference() const override;
};

class HttpBinaryCacheStore : public virtual BinaryCacheStore
{
    struct State
    {
        bool enabled = true;
        std::chrono::steady_clock::time_point disabledUntil;
    };

    Sync<State> _state;

public:

    using Config = HttpBinaryCacheStoreConfig;

    ref<Config> config;

    HttpBinaryCacheStore(ref<Config> config);

    void init() override;

protected:

    std::optional<std::string> getCompressionMethod(const std::string & path);

    void maybeDisable();

    void checkEnabled();

    bool fileExists(const std::string & path) override;

    void upsertFile(
        const std::string & path,
        std::shared_ptr<std::basic_iostream<char>> istream,
        const std::string & mimeType,
        uint64_t sizeHint) override;

    FileTransferRequest makeRequest(const std::string & path);

    void getFile(const std::string & path, Sink & sink) override;

    void getFile(const std::string & path, Callback<std::optional<std::string>> callback) noexcept override;

    std::optional<std::string> getNixCacheInfo() override;

    std::optional<TrustedFlag> isTrustedClient() override;
};

} // namespace nix
