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

constexpr std::array<ExperimentalFeatureDetails, 12> xpFeatureDetails = {{
    {
        .tag = Xp::CaDerivations,
        .name = "ca-derivations",
        .description = R"(
            Allow derivations to be content-addressed in order to prevent
            rebuilds when changes to the derivation do not result in changes to
            the derivation's output. See
            [__contentAddressed](@docroot@/language/advanced-attributes.md#adv-attr-__contentAddressed)
            for details.
        )",
    },
    {
        .tag = Xp::ImpureDerivations,
        .name = "impure-derivations",
        .description = R"(
            Allow derivations to produce non-fixed outputs by setting the
            `__impure` derivation attribute to `true`. An impure derivation can
            have differing outputs each time it is built.

            Example:

            ```
            derivation {
              name = "impure";
              builder = /bin/sh;
              __impure = true; # mark this derivation as impure
              args = [ "-c" "read -n 10 random < /dev/random; echo $random > $out" ];
              system = builtins.currentSystem;
            }
            ```

            Each time this derivation is built, it can produce a different
            output (as the builder outputs random bytes to `$out`).  Impure
            derivations also have access to the network, and only fixed-output
            or other impure derivations can rely on impure derivations. Finally,
            an impure derivation cannot also be
            [content-addressed](#xp-feature-ca-derivations).
        )",
    },
    {
        .tag = Xp::Flakes,
        .name = "flakes",
        .description = R"(
            Enable flakes. See the manual entry for [`nix
            flake`](@docroot@/command-ref/new-cli/nix3-flake.md) for details.
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
            Allow derivation builders to call Nix, and thus build derivations
            recursively.

            Example:

            ```
            with import <nixpkgs> {};

            runCommand "foo"
              {
                 buildInputs = [ nix jq ];
                 NIX_PATH = "nixpkgs=${<nixpkgs>}";
              }
              ''
                hello=$(nix-build -E '(import <nixpkgs> {}).hello.overrideDerivation (args: { name = "recursive-hello"; })')

                mkdir -p $out/bin
                ln -s $hello/bin/hello $out/bin/hello
              ''
            ```

            An important restriction on recursive builders is disallowing
            arbitrary substitutions. For example, running

            ```
            nix-store -r /nix/store/kmwd1hq55akdb9sc7l3finr175dajlby-hello-2.10
            ```

            in the above `runCommand` script would be disallowed, as this could
            lead to derivations with hidden dependencies or breaking
            reproducibility by relying on the current state of the Nix store. An
            exception would be if
            `/nix/store/kmwd1hq55akdb9sc7l3finr175dajlby-hello-2.10` were
            already in the build inputs or built by a previous recursive Nix
            call.
        )",
    },
    {
        .tag = Xp::NoUrlLiterals,
        .name = "no-url-literals",
        .description = R"(
            Disallow unquoted URLs as part of the Nix language syntax. The Nix
            language allows for URL literals, like so:

            ```
            $ nix repl
            Welcome to Nix 2.15.0. Type :? for help.

            nix-repl> http://foo
            "http://foo"
            ```

            But enabling this experimental feature will cause the Nix parser to
            throw an error when encountering a URL literal:

            ```
            $ nix repl --extra-experimental-features 'no-url-literals'
            Welcome to Nix 2.15.0. Type :? for help.

            nix-repl> http://foo
            error: URL literals are disabled

            at «string»:1:1:

            1| http://foo
             | ^

            ```

            While this is currently an experimental feature, unquoted URLs are
            being deprecated and their usage is discouraged.

            The reason is that, as opposed to path literals, URLs have no
            special properties that distinguish them from regular strings, URLs
            containing parameters have to be quoted anyway, and unquoted URLs
            may confuse external tooling.
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
    {
        .tag = Xp::DaemonTrustOverride,
        .name = "daemon-trust-override",
        .description = R"(
            Allow forcing trusting or not trusting clients with
            `nix-daemon`. This is useful for testing, but possibly also
            useful for various experiments with `nix-daemon --stdio`
            networking.
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

    static std::unique_ptr<ReverseXpMap> reverseXpMap = []() {
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

nlohmann::json documentExperimentalFeatures() 
{
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
