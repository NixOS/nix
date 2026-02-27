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

    HttpBinaryCacheStoreConfig(ParsedURL cacheUri, const Store::Config::Params & params);

    ParsedURL cacheUri;

    Setting<std::optional<CompressionAlgo>> narinfoCompression{
        this, std::nullopt, "narinfo-compression", "Compression method for `.narinfo` files."};

    Setting<std::optional<CompressionAlgo>> lsCompression{
        this, std::nullopt, "ls-compression", "Compression method for `.ls` files."};

    Setting<std::optional<CompressionAlgo>> logCompression{
        this,
        std::nullopt,
        "log-compression",
        R"(
          Compression method for `log/*` files. It is recommended to
          use a compression method supported by most web browsers
          (e.g. `brotli`).
        )"};

    Setting<std::optional<AbsolutePath>> tlsCert{
        this, std::nullopt, "tls-certificate", "Path to an optional TLS client certificate in PEM format."};

    Setting<std::optional<AbsolutePath>> tlsKey{
        this, std::nullopt, "tls-private-key", "Path to an optional TLS client certificate private key in PEM format."};

    static const std::string name()
    {
        return "HTTP Binary Cache Store";
    }

    static StringSet uriSchemes();

    static std::string doc();

    ref<Store> openStore(ref<FileTransfer> fileTransfer) const;

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

protected:

    ref<FileTransfer> fileTransfer;

public:

    using Config = HttpBinaryCacheStoreConfig;

    ref<Config> config;

    HttpBinaryCacheStore(ref<Config> config, ref<FileTransfer> fileTransfer = getFileTransfer());

    void init() override;

protected:

    std::optional<CompressionAlgo> getCompressionMethod(const std::string & path);

    void maybeDisable();

    void checkEnabled();

    bool fileExists(const std::string & path) override;

    void upsertFile(
        const std::string & path, RestartableSource & source, const std::string & mimeType, uint64_t sizeHint) override;

    FileTransferRequest makeRequest(std::string_view path);

    /**
     * Uploads data to the binary cache.
     *
     * This is a lower-level method that handles the actual upload after
     * compression has been applied. It does not handle compression or
     * error wrapping - those are the caller's responsibility.
     *
     * @param path The path in the binary cache to upload to
     * @param source The data source (should already be compressed if needed)
     * @param sizeHint Size hint for the data
     * @param mimeType The MIME type of the content
     * @param contentEncoding Optional Content-Encoding header value (e.g., "xz", "br")
     */
    void upload(
        std::string_view path,
        RestartableSource & source,
        uint64_t sizeHint,
        std::string_view mimeType,
        std::optional<Headers> headers);

    void getFile(const std::string & path, Sink & sink) override;

    void getFile(const std::string & path, Callback<std::optional<std::string>> callback) noexcept override;

    std::optional<std::string> getNixCacheInfo() override;

    std::optional<TrustedFlag> isTrustedClient() override;
};

} // namespace nix
