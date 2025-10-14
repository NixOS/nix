#pragma once
///@file

#include "nix/util/error.hh"
#include "nix/util/types.hh"
#include "nix/util/json-non-null.hh"

#include <nlohmann/json_fwd.hpp>

namespace nix {

/**
 * The list of available experimental features.
 *
 * If you update this, don’t forget to also change the map defining
 * their string representation and documentation in the corresponding
 * `.cc` file as well.
 */
enum struct ExperimentalFeature {
    CaDerivations,
    ImpureDerivations,
    Flakes,
    FetchTree,
    NixCommand,
    GitHashing,
    RecursiveNix,
    NoUrlLiterals,
    FetchClosure,
    AutoAllocateUids,
    Cgroups,
    DaemonTrustOverride,
    DynamicDerivations,
    ParseTomlTimestamps,
    ReadOnlyLocalStore,
    LocalOverlayStore,
    ConfigurableImpureEnv,
    MountedSSHStore,
    VerifiedFetches,
    PipeOperators,
    ExternalBuilders,
    BLAKE3Hashes,
};

/**
 * Just because writing `ExperimentalFeature::CaDerivations` is way too long
 */
using Xp = ExperimentalFeature;

/**
 * Parse an experimental feature (enum value) from its name. Experimental
 * feature flag names are hyphenated and do not contain spaces.
 */
const std::optional<ExperimentalFeature> parseExperimentalFeature(const std::string_view & name);

/**
 * Show the name of an experimental feature. This is the opposite of
 * parseExperimentalFeature().
 */
std::string_view showExperimentalFeature(const ExperimentalFeature);

/**
 * Compute the documentation of all experimental features.
 *
 * See `doc/manual` for how this information is used.
 */
nlohmann::json documentExperimentalFeatures();

/**
 * Shorthand for `str << showExperimentalFeature(feature)`.
 */
std::ostream & operator<<(std::ostream & str, const ExperimentalFeature & feature);

/**
 * Parse a set of strings to the corresponding set of experimental
 * features, ignoring (but warning for) any unknown feature.
 */
std::set<ExperimentalFeature> parseFeatures(const StringSet &);

/**
 * An experimental feature was required for some (experimental)
 * operation, but was not enabled.
 */
class MissingExperimentalFeature : public Error
{
public:
    /**
     * The experimental feature that was required but not enabled.
     */
    ExperimentalFeature missingFeature;

    std::string reason;

    MissingExperimentalFeature(ExperimentalFeature missingFeature, std::string reason = "");
};

/**
 * `ExperimentalFeature` is always rendered as a string.
 */
template<>
struct json_avoids_null<ExperimentalFeature> : std::true_type
{};

/**
 * Semi-magic conversion to and from json.
 * See the nlohmann/json readme for more details.
 */
void to_json(nlohmann::json &, const ExperimentalFeature &);
void from_json(const nlohmann::json &, ExperimentalFeature &);

} // namespace nix
