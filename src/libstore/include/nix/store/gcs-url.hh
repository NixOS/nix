#pragma once
///@file
#include "nix/util/url.hh"

#include <optional>
#include <string>
#include <vector>

namespace nix {

/**
 * Parsed Google Cloud Storage `gs://` URL.
 *
 * We deliberately support no `endpoint`/`scheme` in the URL: bearer tokens are
 * host-independent, so a URL-supplied endpoint would let e.g. `fetchurl`
 * exfiltrate the caller's token.
 * A custom endpoint is a store setting only (see `GCSBinaryCacheStoreConfig`).
 */
struct ParsedGCSURL
{
    std::string bucket;
    /**
     * @see ParsedURL::path. This is a vector for the same reason.
     * Unlike ParsedURL::path this doesn't include the leading empty segment,
     * since the bucket name is necessary.
     */
    std::vector<std::string> key;
    /** Billing project for requester-pays buckets (sent as `x-goog-user-project`). */
    std::optional<std::string> userProject;
    /** Object generation (GCS object versioning), passed through as a query parameter. */
    std::optional<std::string> generation;

    static ParsedGCSURL parse(const ParsedURL & uri);

    /**
     * Convert to an HTTP(S) URL against the GCS XML API.
     * Without an endpoint this targets `https://storage.googleapis.com`.
     * Store code passes a custom endpoint URL from operator configuration.
     */
    ParsedURL toHttpsUrl(const std::optional<ParsedURL> & endpoint = std::nullopt) const;

    auto operator<=>(const ParsedGCSURL & other) const = default;
};

} // namespace nix
