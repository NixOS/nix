#pragma once
///@file
#include "nix/store/config.hh"
#include "nix/util/error.hh"
#include "nix/util/url.hh"
#include "nix/util/util.hh"

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace nix {

/**
 * S3 addressing style for bucket access.
 * - Auto: virtual-hosted-style for standard AWS endpoints, path-style for custom endpoints.
 * - Path: always use path-style (bucket in URL path).
 * - Virtual: always use virtual-hosted-style (bucket as hostname prefix; bucket name must not contain dots).
 */
enum class S3AddressingStyle {
    Auto,
    Path,
    Virtual,
};

MakeError(InvalidS3AddressingStyle, Error);

S3AddressingStyle parseS3AddressingStyle(std::string_view style);
std::string_view showS3AddressingStyle(S3AddressingStyle style);

template<>
S3AddressingStyle BaseSetting<S3AddressingStyle>::parse(const std::string & str) const;

template<>
std::string BaseSetting<S3AddressingStyle>::to_string() const;

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
    std::optional<S3AddressingStyle> addressingStyle;
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
     * Convert this ParsedS3URL to an HTTP(S) ParsedURL for use with curl's AWS SigV4 authentication.
     * The scheme defaults to HTTPS but respects the 'scheme' setting and custom endpoint schemes.
     */
    ParsedURL toHttpsUrl() const;

    auto operator<=>(const ParsedS3URL & other) const = default;
};

} // namespace nix
