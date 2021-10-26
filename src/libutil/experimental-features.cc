#include "experimental-features.hh"
#include "nlohmann/json.hpp"

namespace nix {

std::map<ExperimentalFeature, std::string> stringifiedXpFeatures = {
    { Xp::CaDerivations, "ca-derivations" },
    { Xp::Flakes, "flakes" },
    { Xp::NixCommand, "nix-command" },
    { Xp::RecursiveNix, "recursive-nix" },
    { Xp::NoUrlLiterals, "no-url-literals" },
};

const std::optional<ExperimentalFeature> parseExperimentalFeature(const std::string_view & name)
{
    using ReverseXpMap = std::map<std::string_view, ExperimentalFeature>;
    static ReverseXpMap * reverseXpMap;
    if (!reverseXpMap) {
        reverseXpMap = new ReverseXpMap{};
        for (auto & [feature, name] : stringifiedXpFeatures)
            (*reverseXpMap)[name] = feature;
    }

    auto featureIter = reverseXpMap->find(name);
    if (featureIter == reverseXpMap->end())
        return std::nullopt;
    return {featureIter->second};
}

std::string_view showExperimentalFeature(const ExperimentalFeature feature)
{
    return stringifiedXpFeatures.at(feature);
}

std::set<ExperimentalFeature> parseFeatures(const std::set<std::string> & rawFeatures)
{
    std::set<ExperimentalFeature> res;
    for (auto & rawFeature : rawFeatures) {
        if (auto feature = parseExperimentalFeature(rawFeature))
            res.insert(*feature);
    }
    return res;
}

MissingExperimentalFeature::MissingExperimentalFeature(ExperimentalFeature feature)
    : Error("experimental Nix feature '%1%' is disabled; use '--extra-experimental-features %1%' to override", showExperimentalFeature(feature))
    , missingFeature(feature)
    {}

std::ostream & operator <<(std::ostream & str, const ExperimentalFeature & feature)
{
    return str << showExperimentalFeature(feature);
}

}
