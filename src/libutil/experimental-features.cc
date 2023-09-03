#include "experimental-features.hh"
#include "util.hh"

#include "nlohmann/json.hpp"

namespace nix {

std::map<ExperimentalFeature, std::string> stringifiedXpFeatures = {
    { Xp::CaDerivations, "ca-derivations" },
    { Xp::ImpureDerivations, "impure-derivations" },
    { Xp::Flakes, "flakes" },
    { Xp::NixCommand, "nix-command" },
    { Xp::RecursiveNix, "recursive-nix" },
    { Xp::NoUrlLiterals, "no-url-literals" },
    { Xp::FetchClosure, "fetch-closure" },
    { Xp::ReplFlake, "repl-flake" },
    { Xp::AutoAllocateUids, "auto-allocate-uids" },
    { Xp::Cgroups, "cgroups" },
};

const std::optional<ExperimentalFeature> parseExperimentalFeature(const std::string_view & name)
{
    using ReverseXpMap = std::map<std::string_view, ExperimentalFeature>;

    static auto reverseXpMap = []()
    {
        auto reverseXpMap = std::make_unique<ReverseXpMap>();
        for (auto & [feature, name] : stringifiedXpFeatures)
            (*reverseXpMap)[name] = feature;
        return reverseXpMap;
    }();

    if (auto feature = get(*reverseXpMap, name))
        return *feature;
    else
        return std::nullopt;
}

std::string_view showExperimentalFeature(const ExperimentalFeature feature)
{
    const auto ret = get(stringifiedXpFeatures, feature);
    assert(ret);
    return *ret;
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

void to_json(nlohmann::json & j, const ExperimentalFeature & feature)
{
    j = showExperimentalFeature(feature);
}

void from_json(const nlohmann::json & j, ExperimentalFeature & feature)
{
    const std::string input = j;
    const auto parsed = parseExperimentalFeature(input);

    if (parsed.has_value())
        feature = *parsed;
    else
        throw Error("Unknown experimental feature '%s' in JSON input", input);
}

}
