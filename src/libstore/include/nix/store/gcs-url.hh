#pragma once
///@file

#include "nix/util/url.hh"

#include <string>
#include <vector>

namespace nix {

/**
 * Parsed gs:// URL for Google Cloud Storage
 */
struct ParsedGcsURL
{
    std::string bucket;
    std::vector<std::string> key;
    /**
     * Whether write access is requested (via ?write=true query param).
     * Defaults to false (read-only).
     */
    bool writable = false;

    /**
     * Parse a gs:// URL.
     *
     * @param parsed The parsed URL to convert
     * @return ParsedGcsURL with bucket and key extracted
     * @throws BadURL if the URL is not a valid gs:// URL
     */
    static ParsedGcsURL parse(const ParsedURL & parsed);

    /**
     * Convert to HTTPS URL for storage.googleapis.com
     */
    ParsedURL toHttpsUrl() const;

    auto operator<=>(const ParsedGcsURL & other) const = default;
};

} // namespace nix
