#include <optional>
#include <string>

namespace nix {
struct ParsedURL;

/**
 * Try to extract a reasonably unique and meaningful, human-readable
 * name of a flake output from a parsed URL.
 * When nullopt is returned, the callsite should use information available
 * to it outside of the URL to determine a useful name.
 * This is a heuristic approach intended for user interfaces.
 * @return nullopt if the extracted name is not useful to identify a
 * flake output, for example because it is empty or "default".
 * Otherwise returns the extracted name.
 */
std::optional<std::string> getNameFromURL(const ParsedURL & url);

} // namespace nix
