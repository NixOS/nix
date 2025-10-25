#pragma once
///@file
#include "nix/store/config.hh"
#include "nix/util/url.hh"
#include "nix/util/util.hh"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace nix {

/**
 * Parsed S3 URL.
 */
struct ParsedS3URL
{
    std::string bucket;
    /**
     * @see ParsedURL::path. This is a vector for the same reason.
     * Unlike ParsedURL::path this doesn't include the leading empty segment,
     * since the bucket name is necessary.
     */
    std::vector<std::string> key;
    std::optional<std::string> profile;
    std::optional<std::string> region;
    std::optional<std::string> scheme;
    std::optional<std::string> versionId;
    /**
     * The endpoint can be either missing, be an absolute URI (with a scheme like `http:`)
     * or an authority (so an IP address or a registered name).
     */
    std::variant<std::monostate, ParsedURL, ParsedURL::Authority> endpoint;

    std::optional<std::string> getEncodedEndpoint() const
    {
        return std::visit(
            overloaded{
                [](std::monostate) -> std::optional<std::string> { return std::nullopt; },
                [](const auto & authorityOrUrl) -> std::optional<std::string> { return authorityOrUrl.to_string(); },
            },
            endpoint);
    }

    static ParsedS3URL parse(const ParsedURL & uri);

    /**
     * Convert this ParsedS3URL to HTTPS ParsedURL for use with curl's AWS SigV4 authentication
     */
    ParsedURL toHttpsUrl() const;

    auto operator<=>(const ParsedS3URL & other) const = default;
};

} // namespace nix
