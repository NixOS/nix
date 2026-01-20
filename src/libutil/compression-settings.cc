#include "nix/util/configuration.hh"
#include "nix/util/compression-settings.hh"
#include "nix/util/json-impls.hh"
#include "nix/util/config-impl.hh"
#include "nix/util/abstract-setting-to-json.hh"

#include <nlohmann/json.hpp>

namespace nix {

template<>
CompressionAlgo BaseSetting<CompressionAlgo>::parse(const std::string & str) const
try {
    return parseCompressionAlgo(str, /*suggestions=*/true);
} catch (UnknownCompressionMethod & e) {
    throw UsageError(e.info().suggestions, "option '%s' has invalid value '%s'", name, str);
}

template<>
std::optional<CompressionAlgo> BaseSetting<std::optional<CompressionAlgo>>::parse(const std::string & str) const
try {
    if (str.empty())
        return std::nullopt;
    return parseCompressionAlgo(str, /*suggestions=*/true);
} catch (UnknownCompressionMethod & e) {
    throw UsageError(e.info().suggestions, "option '%s' has invalid value '%s'", name, str);
}

template<>
struct BaseSetting<CompressionAlgo>::trait
{
    static constexpr bool appendable = false;
};

template<>
struct BaseSetting<std::optional<CompressionAlgo>>::trait
{
    static constexpr bool appendable = false;
};

template<>
std::string BaseSetting<CompressionAlgo>::to_string() const
{
    return std::string{showCompressionAlgo(value)};
}

template<>
std::string BaseSetting<std::optional<CompressionAlgo>>::to_string() const
{
    if (value)
        return std::string{showCompressionAlgo(*value)};
    return "";
}

/* Same as with all settings - empty string means std::nullopt. */
template<>
struct json_avoids_null<CompressionAlgo> : std::true_type
{};

#define NIX_COMPRESSION_JSON(name, value) {CompressionAlgo::value, name},
NLOHMANN_JSON_SERIALIZE_ENUM(CompressionAlgo, {NIX_FOR_EACH_COMPRESSION_ALGO(NIX_COMPRESSION_JSON)});
#undef NIX_COMPRESSION_JSON

/* Explicit instantiation of templates */
template class BaseSetting<CompressionAlgo>;
template class BaseSetting<std::optional<CompressionAlgo>>;

} // namespace nix
