#include "nix/store/gcs-url.hh"
#include "nix/util/error.hh"

namespace nix {

ParsedGcsURL ParsedGcsURL::parse(const ParsedURL & parsed)
try {
    if (parsed.scheme != "gs")
        throw BadURL("URI scheme '%s' is not 'gs'", parsed.scheme);

    if (!parsed.authority || parsed.authority->host.empty()
        || parsed.authority->hostType != ParsedURL::Authority::HostType::Name)
        throw BadURL("URI has a missing or invalid bucket name");

    if (parsed.path.size() <= 1 || !parsed.path.front().empty())
        throw BadURL("URI has a missing or invalid key");

    // Skip the first empty path segment (from leading /)
    std::vector<std::string> key(parsed.path.begin() + 1, parsed.path.end());

    // Check for write=true query parameter
    bool writable = false;
    auto it = parsed.query.find("write");
    if (it != parsed.query.end() && it->second == "true") {
        writable = true;
    }

    return ParsedGcsURL{
        .bucket = parsed.authority->host,
        .key = std::move(key),
        .writable = writable,
    };
} catch (BadURL & e) {
    e.addTrace({}, "while parsing GCS URI: '%s'", parsed.to_string());
    throw;
}

ParsedURL ParsedGcsURL::toHttpsUrl() const
{
    std::vector<std::string> path{""};
    path.push_back(bucket);
    path.insert(path.end(), key.begin(), key.end());

    return ParsedURL{
        .scheme = "https",
        .authority = ParsedURL::Authority{.host = "storage.googleapis.com"},
        .path = std::move(path),
    };
}

} // namespace nix
