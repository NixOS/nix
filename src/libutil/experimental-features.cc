#include "nix/util/experimental-features.hh"
#include "nix/util/fmt.hh"
#include "nix/util/strings.hh"
#include "nix/util/util.hh"

#include <nlohmann/json.hpp>

namespace nix {

struct ExperimentalFeatureDetails
{
    ExperimentalFeature tag;
    std::string_view name;
    std::string_view description;
    std::string_view trackingUrl;
};

/**
 * If two different PRs both add an experimental feature, and we just
 * used a number for this, we *woudln't* get merge conflict and the
 * counter will be incremented once instead of twice, causing a build
 * failure.
 *
 * By instead defining this instead as 1 + the bottom experimental
 * feature, we either have no issue at all if few features are not added
 * at the end of the list, or a proper merge conflict if they are.
 */
constexpr size_t numXpFeatures = 1 + static_cast<size_t>(Xp::FunctionSerialization);

constexpr std::array<ExperimentalFeatureDetails, numXpFeatures> xpFeatureDetails = {{
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
        .trackingUrl = "https://github.com/NixOS/nix/milestone/35",
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

            This is a more explicit alternative to using [`builtins.currentTime`](@docroot@/language/builtins.md#builtins-currentTime).
        )",
        .trackingUrl = "https://github.com/NixOS/nix/milestone/42",
    },
    {
        .tag = Xp::Flakes,
        .name = "flakes",
        .description = R"(
            Enable flakes. See the manual entry for [`nix
            flake`](@docroot@/command-ref/new-cli/nix3-flake.md) for details.
        )",
        .trackingUrl = "https://github.com/NixOS/nix/milestone/27",
    },
    {
        .tag = Xp::FetchTree,
        .name = "fetch-tree",
        .description = R"(
            Enable the use of the [`fetchTree`](@docroot@/language/builtins.md#builtins-fetchTree) built-in function in the Nix language.

            `fetchTree` exposes a generic interface for fetching remote file system trees from different types of remote sources.
            The [`flakes`](#xp-feature-flakes) feature flag always enables `fetch-tree`.
            This built-in was previously guarded by the `flakes` experimental feature because of that overlap.

            Enabling just this feature serves as a "release candidate", allowing users to try it out in isolation.
        )",
        .trackingUrl = "https://github.com/NixOS/nix/milestone/31",
    },
    {
        .tag = Xp::NixCommand,
        .name = "nix-command",
        .description = R"(
            Enable the new `nix` subcommands. See the manual on
            [`nix`](@docroot@/command-ref/new-cli/nix.md) for details.
        )",
        .trackingUrl = "https://github.com/NixOS/nix/milestone/28",
    },
    {
        .tag = Xp::GitHashing,
        .name = "git-hashing",
        .description = R"(
            Allow creating (content-addressed) store objects which are hashed via Git's hashing algorithm.
            These store objects aren't understandable by older versions of Nix.
        )",
        .trackingUrl = "https://github.com/NixOS/nix/milestone/41",
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
                 # Optional: let Nix know "foo" requires the experimental feature
                 requiredSystemFeatures = [ "recursive-nix" ];
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
            nix-store -r /nix/store/lrs9qfm60jcgsk83qhyypj3m4jqsgdid-hello-2.10
            ```

            in the above `runCommand` script would be disallowed, as this could
            lead to derivations with hidden dependencies or breaking
            reproducibility by relying on the current state of the Nix store. An
            exception would be if
            `/nix/store/lrs9qfm60jcgsk83qhyypj3m4jqsgdid-hello-2.10` were
            already in the build inputs or built by a previous recursive Nix
            call.
        )",
        .trackingUrl = "https://github.com/NixOS/nix/milestone/47",
    },
    {
        .tag = Xp::FetchClosure,
        .name = "fetch-closure",
        .description = R"(
            Enable the use of the [`fetchClosure`](@docroot@/language/builtins.md#builtins-fetchClosure) built-in function in the Nix language.
        )",
        .trackingUrl = "https://github.com/NixOS/nix/milestone/40",
    },
    {
        .tag = Xp::AutoAllocateUids,
        .name = "auto-allocate-uids",
        .description = R"(
            Allows Nix to automatically pick UIDs for builds, rather than creating
            `nixbld*` user accounts. See the [`auto-allocate-uids`](@docroot@/command-ref/conf-file.md#conf-auto-allocate-uids) setting for details.
        )",
        .trackingUrl = "https://github.com/NixOS/nix/milestone/34",
    },
    {
        .tag = Xp::Cgroups,
        .name = "cgroups",
        .description = R"(
            Allows Nix to execute builds inside cgroups. See
            the [`use-cgroups`](@docroot@/command-ref/conf-file.md#conf-use-cgroups) setting for details.
        )",
        .trackingUrl = "https://github.com/NixOS/nix/milestone/36",
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
        .trackingUrl = "https://github.com/NixOS/nix/milestone/38",
    },
    {
        .tag = Xp::DynamicDerivations,
        .name = "dynamic-derivations",
        .description = R"(
            Allow the use of a few things related to dynamic derivations:

              - "text hashing" derivation outputs, so we can build .drv
                files.

              - dependencies in derivations on the outputs of
                derivations that are themselves derivations outputs.
        )",
        .trackingUrl = "https://github.com/NixOS/nix/milestone/39",
    },
    {
        .tag = Xp::ParseTomlTimestamps,
        .name = "parse-toml-timestamps",
        .description = R"(
            Allow parsing of timestamps in builtins.fromTOML.
        )",
        .trackingUrl = "https://github.com/NixOS/nix/milestone/45",
    },
    {
        .tag = Xp::ReadOnlyLocalStore,
        .name = "read-only-local-store",
        .description = R"(
            Allow the use of the `read-only` parameter in [local store](@docroot@/store/types/local-store.md) URIs.
        )",
        .trackingUrl = "https://github.com/NixOS/nix/milestone/46",
    },
    {
        .tag = Xp::LocalOverlayStore,
        .name = "local-overlay-store",
        .description = R"(
            Allow the use of [local overlay store](@docroot@/command-ref/new-cli/nix3-help-stores.md#experimental-local-overlay-store).
        )",
        .trackingUrl = "https://github.com/NixOS/nix/milestone/50",
    },
    {
        .tag = Xp::ConfigurableImpureEnv,
        .name = "configurable-impure-env",
        .description = R"(
            Allow the use of the [impure-env](@docroot@/command-ref/conf-file.md#conf-impure-env) setting.
        )",
        .trackingUrl = "https://github.com/NixOS/nix/milestone/37",
    },
    {
        .tag = Xp::MountedSSHStore,
        .name = "mounted-ssh-store",
        .description = R"(
            Allow the use of the [`mounted SSH store`](@docroot@/command-ref/new-cli/nix3-help-stores.html#experimental-ssh-store-with-filesystem-mounted).
        )",
        .trackingUrl = "https://github.com/NixOS/nix/milestone/43",
    },
    {
        .tag = Xp::VerifiedFetches,
        .name = "verified-fetches",
        .description = R"(
            Enables verification of git commit signatures through the [`fetchGit`](@docroot@/language/builtins.md#builtins-fetchGit) built-in.
        )",
        .trackingUrl = "https://github.com/NixOS/nix/milestone/48",
    },
    {
        .tag = Xp::PipeOperators,
        .name = "pipe-operators",
        .description = R"(
            Add `|>` and `<|` operators to the Nix language.
        )",
        .trackingUrl = "https://github.com/NixOS/nix/milestone/55",
    },
    {
        .tag = Xp::ExternalBuilders,
        .name = "external-builders",
        .description = R"(
            Enables support for external builders / sandbox providers.
        )",
        .trackingUrl = "https://github.com/NixOS/nix/milestone/62",
    },
    {
        .tag = Xp::BLAKE3Hashes,
        .name = "blake3-hashes",
        .description = R"(
            Enables support for BLAKE3 hashes.
        )",
        .trackingUrl = "https://github.com/NixOS/nix/milestone/60",
    },
    {
        .tag = Xp::FunctionSerialization,
        .name = "function-serialization",
        .description = R"(
            Allow serializing and deserializing Nix functions.

            When enabled, `builtins.toString` can coerce functions to
            strings, and two new builtins become available:

            - `builtins.serializeFunction` produces a self-contained Nix
              expression string for a lambda, including `let` bindings
              for captured closure variables.

            - `builtins.deserializeFunction` parses a string as a Nix
              expression that must evaluate to a function.

            ## Relation to dynamic derivations

            This feature serializes *pure computation* -- functions that
            transform data.  It is useful for bundling eval-time logic
            and captured configuration into a single string that can be
            passed into a build sandbox and evaluated there (with
            recursive Nix).

            For the core dynamic derivations use case of constructing
            real derivations with real store path dependencies, enable
            `preserveStringContext` in the attrset form to emit
            `builtins.appendContext` calls that reconstruct store path
            contexts on deserialization.

            ### What works today

            The serialize/deserialize pipeline handles: closures with
            captured configuration, dependency maps, higher-order
            builders, partially applied primops, `lib` polyfills (via
            eta-reduction), `with`-bound variables, recursive values,
            and (with `preserveStringContext`) store path dependencies.

            Existing approaches that this feature complements:
            - Raw `.drv` format (no Nix evaluator needed in sandbox)
            - Recursive Nix with a static `.nix` file + JSON data
            - `nix-instantiate` inside the sandbox

            This feature adds the ability to bundle eval-time logic and
            data into a single string, avoiding the split between "JSON
            data" and "static `.nix` file" in the approaches above.

            ### Handling `lib` function polyfills

            nixpkgs `lib` often wraps builtins in lambda polyfills for
            backwards compatibility (e.g. `lib.head = x: builtins.head x`
            instead of `lib.head = builtins.head`).  This poses a
            challenge: the same logical function can be either a primop
            or a lambda depending on the nixpkgs/Nix version, producing
            different serialized forms.

            The serializer handles this via eta-reduction: lambdas whose
            body is a direct call to a closure variable or a `builtins.*`
            select, with the lambda's parameters forwarded in order, are
            reduced to the underlying function.  Three patterns are
            detected:

            - Closure variable: `let head = builtins.head; in (x: head x)`
              serializes as `builtins.head`
            - Direct builtin select: `x: builtins.head x` serializes as
              `builtins.head`
            - Multi-arg: `list: index: builtins.elemAt list index`
              serializes as `builtins.elemAt`

            This means `lib.head` and `builtins.head` produce identical
            serialized output regardless of whether the polyfill is in
            place.

            Patterns not handled by eta-reduction:

            - Wrappers that reorder, drop, or add arguments
              (e.g. `lib.flip f a b = f b a`)
            - Wrappers that add validation or coercion before calling
              the underlying function
            - Wrappers where the called function is computed dynamically
              (e.g. via `builtins.getAttr`)

            These cases serialize as full lambdas with closure bindings,
            which is correct but produces different output than a direct
            primop reference.  The serialized form is still valid and
            will deserialize correctly within the same Nix version.

            ### Interaction with `derivation` and `import`

            The `derivation` function and other top-level constants
            (e.g. `derivationStrict`, `import`) are available after
            deserialization because `builtins.deserializeFunction`
            parses in the base environment.  Functions that call
            `derivation` round-trip correctly:

            ```nix
            let mkDrv = builtins.deserializeFunction
              (builtins.serializeFunction (name: derivation {
                inherit name; system = "x86_64-linux"; builder = "/bin/sh";
              }));
            in (mkDrv "hello").name   # => "hello"
            ```

            `import` is a primop and serializes as `builtins.import`.
            The only issue is file availability: paths in `import`
            calls are absolute (resolved at parse time) and must exist
            in the deserialization environment.

            ### Remaining gaps for dynamic derivations

            - **No `.drv` file creation in sandbox**: deserialized
              functions can produce derivation attrsets and even call
              `derivation` to create them, but the resulting `.drv`
              files can only be registered in the store via recursive
              Nix (the restricted daemon socket, issue #8602).  This is
              a store/scheduler feature, not an evaluator feature.

            ## Known limitations

            - **String context loss by default**: store path dependency
              contexts are lost on round-trip unless
              `preserveStringContext = true` is set in the attrset form.
              When enabled, strings with context are wrapped in
              `builtins.appendContext` calls in the serialized output.

            - **Serialized form is not stable across Nix versions.**
              Primops are serialized as `builtins.<name>`, which may
              change between versions.  Eta-reduction normalizes common
              `lib` polyfill patterns (see above), but not all
              variations can be detected.  The format should only be
              used within a single Nix version, not for persistent
              storage.

            - **User-provided primops** (from plugins or the flake
              subsystem) are rejected by default at serialization time,
              since the plugin may not be loaded in the deserialization
              environment.  The `allowedPluginPrimOps` option accepts a
              list of names presumed available at deserialization time.

            - Float precision is limited by `std::to_string` (6 decimal
              places).  Fixable with a higher-precision formatter, but
              this changes float value serialization.

            - Paths are serialized as absolute paths (the Nix parser
              resolves relative paths at parse time).  If the referenced
              file does not exist in the deserialization environment
              (e.g. a build sandbox), operations like `import` or string
              interpolation on the path will fail.

            - `builtins.deserializeFunction` evaluates arbitrary Nix
              expressions; only use it on trusted input.  This is
              inherent to the string-eval approach.
        )",
    },
}};

static_assert(
    []() constexpr {
        for (auto [index, feature] : enumerate(xpFeatureDetails))
            if (index != (size_t) feature.tag)
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
    assert((size_t) tag < xpFeatureDetails.size());
    return xpFeatureDetails[(size_t) tag].name;
}

nlohmann::json documentExperimentalFeatures()
{
    StringMap res;
    for (auto & xpFeature : xpFeatureDetails) {
        std::stringstream docOss;
        docOss << stripIndentation(xpFeature.description);
        docOss << fmt(
            "\nRefer to [%1% tracking issue](%2%) for feature tracking.", xpFeature.name, xpFeature.trackingUrl);
        res[std::string{xpFeature.name}] = trim(docOss.str());
    }
    return (nlohmann::json) res;
}

std::set<ExperimentalFeature> parseFeatures(const StringSet & rawFeatures)
{
    std::set<ExperimentalFeature> res;
    for (auto & rawFeature : rawFeatures)
        if (auto feature = parseExperimentalFeature(rawFeature))
            res.insert(*feature);
    return res;
}

MissingExperimentalFeature::MissingExperimentalFeature(ExperimentalFeature feature, std::string reason)
    : CloneableError(
          "experimental Nix feature '%1%' is disabled%2%; add '--extra-experimental-features %1%' to enable it",
          showExperimentalFeature(feature),
          Uncolored(optionalBracket(" (", reason, ")")))
    , missingFeature(feature)
    , reason{reason}
{
}

std::ostream & operator<<(std::ostream & str, const ExperimentalFeature & feature)
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

} // namespace nix
