#include "experimental-features.hh"
#include "util.hh"

#include "nlohmann/json.hpp"

namespace nix {

struct ExperimentalFeatureDetails
{
    ExperimentalFeature tag;
    std::string_view name;
    std::string_view description;
};

constexpr std::array<ExperimentalFeatureDetails, 11> xpFeatureDetails = {{
    {
        .tag = Xp::CaDerivations,
        .name = "ca-derivations",
        .description = R"(
            Allow derivations to be content-addressed in order to prevent rebuilds
            when changes to the derivation do not result in changes to the
            derivation's output. See
            [__contentAddressed](@docroot@/language/advanced-attributes.md#adv-attr-__contentAddressed)
            for details.
        )",
    },
    {
        .tag = Xp::ImpureDerivations,
        .name = "impure-derivations",
        .description = R"(
            Allows derivations to produce non-fixed outputs by setting the `__impure`
            derivation attribute to `true`. See [these release
            notes](../release-notes/rl-2.8.md) for an example.
        )",
    },
    {
        .tag = Xp::Flakes,
        .name = "flakes",
        .description = R"(
            Enable flakes. See the manual entry for
            [`nix flake`](../command-ref/new-cli/nix3-flake.md) for details.
        )",
    },
    {
        .tag = Xp::NixCommand,
        .name = "nix-command",
        .description = R"(
            Enable the new `nix` subcommands. See the manual on
            [`nix`](@docroot@/command-ref/new-cli/nix.md) for details.
        )",
    },
    {
        .tag = Xp::RecursiveNix,
        .name = "recursive-nix",
        .description = R"(
            Allow Nix derivations to call Nix in order to recursively build derivations.
            See [this
            commit](https://github.com/edolstra/nix/commit/1a27aa7d64ffe6fc36cfca4d82bdf51c4d8cf717)
            for more info.
        )",
    },
    {
        .tag = Xp::NoUrlLiterals,
        .name = "no-url-literals",
        .description = R"(
            Disallows unquoted URLs as part of the Nix language syntax. See [RFC
            45](https://github.com/NixOS/rfcs/pull/45) for more info.
        )",
    },
    {
        .tag = Xp::FetchClosure,
        .name = "fetch-closure",
        .description = R"(
            Enable the use of the [`fetchClosure`](@docroot@/language/builtins.md#builtins-fetchClosure) built-in function in the Nix language.
        )",
    },
    {
        .tag = Xp::ReplFlake,
        .name = "repl-flake",
        .description = R"(
            Allow passing [installables](@docroot@/command-ref/new-cli/nix.md#installables) to `nix repl`, making its interface consistent with the other experimental commands.
        )",
    },
    {
        .tag = Xp::AutoAllocateUids,
        .name = "auto-allocate-uids",
        .description = R"(
            Allows Nix to automatically pick UIDs for builds, rather than creating
            `nixbld*` user accounts. See the [`auto-allocate-uids`](#conf-auto-allocate-uids) setting for details.
        )",
    },
    {
        .tag = Xp::Cgroups,
        .name = "cgroups",
        .description = R"(
            Allows Nix to execute builds inside cgroups. See
            the [`use-cgroups`](#conf-use-cgroups) setting for details.
        )",
    },
    {
        .tag = Xp::DiscardReferences,
        .name = "discard-references",
        .description = R"(
            Allow the use of the [`unsafeDiscardReferences`](@docroot@/language/advanced-attributes.html#adv-attr-unsafeDiscardReferences) attribute in derivations
            that use [structured attributes](@docroot@/language/advanced-attributes.html#adv-attr-structuredAttrs). This disables scanning of outputs for
            runtime dependencies.
        )",
    },
}};

static_assert(
    []() constexpr {
        for (auto [index, feature] : enumerate(xpFeatureDetails))
            if (index != (size_t)feature.tag)
                return false;
        return true;
    }(),
    "array order does not match enum tag order");

const std::optional<ExperimentalFeature> parseExperimentalFeature(const std::string_view & name)
{
    using ReverseXpMap = std::map<std::string_view, ExperimentalFeature>;

    static std::unique_ptr<ReverseXpMap> reverseXpMap = [](){
        auto reverseXpMap = std::make_unique<ReverseXpMap>();
        for (auto & xpFeature : xpFeatureDetails)
            (*reverseXpMap)[xpFeature.name] = xpFeature.tag;
        return reverseXpMap;
    }();

    if (auto feature = get(*reverseXpMap, name))
        return *feature;
    else
        return std::nullopt;
}

std::string_view showExperimentalFeature(const ExperimentalFeature tag)
{
    assert((size_t)tag < xpFeatureDetails.size());
    return xpFeatureDetails[(size_t)tag].name;
}

nlohmann::json documentExperimentalFeatures() {
    StringMap res;
    for (auto & xpFeature : xpFeatureDetails)
        res[std::string { xpFeature.name }] =
            trim(stripIndentation(xpFeature.description));
    return (nlohmann::json) res;
}

std::set<ExperimentalFeature> parseFeatures(const std::set<std::string> & rawFeatures)
{
    std::set<ExperimentalFeature> res;
    for (auto & rawFeature : rawFeatures)
        if (auto feature = parseExperimentalFeature(rawFeature))
            res.insert(*feature);
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
