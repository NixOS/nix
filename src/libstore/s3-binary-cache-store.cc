#include "nix/store/s3-binary-cache-store.hh"
#include "nix/store/http-binary-cache-store.hh"
#include "nix/store/store-registration.hh"
#include "nix/util/error.hh"
#include "nix/util/logging.hh"

#include <cassert>
#include <ranges>

namespace nix {

StringSet S3BinaryCacheStoreConfig::uriSchemes()
{
    return {"s3"};
}

S3BinaryCacheStoreConfig::S3BinaryCacheStoreConfig(
    std::string_view scheme, std::string_view _cacheUri, const Params & params)
    : StoreConfig(params)
    , HttpBinaryCacheStoreConfig(scheme, _cacheUri, params)
{
    assert(cacheUri.query.empty());
    assert(cacheUri.scheme == "s3");

    for (const auto & [key, value] : params) {
        auto s3Params =
            std::views::transform(s3UriSettings, [](const AbstractSetting * setting) { return setting->name; });
        if (std::ranges::contains(s3Params, key)) {
            cacheUri.query[key] = value;
        }
    }

    const uint64_t AWS_MIN_PART_SIZE = 5 * 1024 * 1024;
    const uint64_t AWS_MAX_PART_SIZE = 5ULL * 1024 * 1024 * 1024;

    if (multipartChunkSize < AWS_MIN_PART_SIZE) {
        throw UsageError(
            "multipart-chunk-size must be at least 5 MiB (5242880 bytes), got %d bytes (%d MiB)",
            multipartChunkSize.get(),
            multipartChunkSize.get() / (1024 * 1024));
    }

    if (multipartChunkSize > AWS_MAX_PART_SIZE) {
        throw UsageError(
            "multipart-chunk-size must be at most 5 GiB (5368709120 bytes), got %d bytes (%d GiB)",
            multipartChunkSize.get(),
            multipartChunkSize.get() / (1024 * 1024 * 1024));
    }

    if (multipartUpload && multipartThreshold < multipartChunkSize) {
        warn(
            "multipart-threshold (%d MiB) is less than multipart-chunk-size (%d MiB), "
            "which may result in single-part multipart uploads",
            multipartThreshold.get() / (1024 * 1024),
            multipartChunkSize.get() / (1024 * 1024));
    }
}

std::string S3BinaryCacheStoreConfig::getHumanReadableURI() const
{
    auto reference = getReference();
    reference.params = [&]() {
        Params relevantParams;
        for (auto & setting : s3UriSettings)
            if (setting->overridden)
                relevantParams.insert({setting->name, reference.params.at(setting->name)});
        return relevantParams;
    }();
    return reference.render();
}

std::string S3BinaryCacheStoreConfig::doc()
{
    return R"(
        **Store URL format**: `s3://bucket-name`

        This store allows reading and writing a binary cache stored in an AWS S3 bucket.
    )";
}

static RegisterStoreImplementation<S3BinaryCacheStoreConfig> registerS3BinaryCacheStore;

} // namespace nix
