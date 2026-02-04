#include "nix/util/deprecated-features.hh"
#include "nix/util/fmt.hh"
#include "nix/util/strings.hh"
#include "nix/util/util.hh"

#include <nlohmann/json.hpp>

namespace nix {

struct DeprecatedFeatureDetails
{
    DeprecatedFeature tag;
    std::string_view name;
    std::string_view description;
};

// Add features here
constexpr std::array<DeprecatedFeatureDetails, 1> depFeatureDetails = {{{
    .tag = DeprecatedFeature::UrlLiterals,
    .name = "url-literals",
    .description = R"(
            Re-enable support for URL literals.
        )",
}}};

const std::optional<DeprecatedFeature> parseDeprecatedFeature(const std::string_view & name)
{
    using ReverseDepMap = std::map<std::string_view, DeprecatedFeature>;

    static std::unique_ptr<ReverseDepMap> reverseDepMap = []() {
        auto reverseDepMap = std::make_unique<ReverseDepMap>();
        for (auto & depFeature : depFeatureDetails)
            (*reverseDepMap)[depFeature.name] = depFeature.tag;
        return reverseDepMap;
    }();

    if (auto feature = get(*reverseDepMap, name))
        return *feature;
    else
        return std::nullopt;
}

std::string_view showDeprecatedFeature(const DeprecatedFeature tag)
{
    for (const auto & detail : depFeatureDetails) {
        if (detail.tag == tag)
            return detail.name;
    }
    throw Error("Unknown deprecated feature tag");
}

nlohmann::json documentDeprecatedFeatures()
{
    std::map<std::string, std::string> res;
    for (auto & depFeature : depFeatureDetails)
        res[std::string{depFeature.name}] = trim(stripIndentation(depFeature.description));
    return (nlohmann::json) res;
}

std::set<DeprecatedFeature> parseDeprecatedFeatures(const std::set<std::string> & rawFeatures)
{
    std::set<DeprecatedFeature> res;
    for (auto & rawFeature : rawFeatures)
        if (auto feature = parseDeprecatedFeature(rawFeature))
            res.insert(*feature);
        else
            warn("unknown deprecated feature '%s'", rawFeature);
    return res;
}

MissingDeprecatedFeature::MissingDeprecatedFeature(DeprecatedFeature feature)
    : Error(
          "Feature '%1%' is deprecated and should not be used anymore; use '--extra-deprecated-features %1%' to disable this error",
          showDeprecatedFeature(feature))
    , missingFeature(feature)
{
}

std::ostream & operator<<(std::ostream & str, const DeprecatedFeature & feature)
{
    return str << showDeprecatedFeature(feature);
}

void to_json(nlohmann::json & j, const DeprecatedFeature & feature)
{
    j = showDeprecatedFeature(feature);
}

void from_json(const nlohmann::json & j, DeprecatedFeature & feature)
{
    const std::string input = j;
    const auto parsed = parseDeprecatedFeature(input);

    if (parsed.has_value())
        feature = *parsed;
    else
        throw Error("Unknown deprecated feature '%s' in JSON input", input);
}

} // namespace nix
