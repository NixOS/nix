#pragma once
///@file

#include "comparator.hh"
#include "error.hh"
#include "json-utils.hh"
#include "types.hh"

namespace nix {

/**
 * The list of available experimental features.
 *
 * If you update this, don’t forget to also change the map defining
 * their string representation and documentation in the corresponding
 * `.cc` file as well.
 */
enum struct ExperimentalFeature
{
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
};

/**
 * Just because writing `ExperimentalFeature::CaDerivations` is way too long
 */
using Xp = ExperimentalFeature;

/**
 * Parse an experimental feature (enum value) from its name. Experimental
 * feature flag names are hyphenated and do not contain spaces.
 */
const std::optional<ExperimentalFeature> parseExperimentalFeature(
        const std::string_view & name);

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
std::ostream & operator<<(
        std::ostream & str,
        const ExperimentalFeature & feature);

/**
 * Parse a set of strings to the corresponding set of experimental
 * features, ignoring (but warning for) any unknown feature.
 */
std::set<ExperimentalFeature> parseFeatures(const std::set<std::string> &);

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

    MissingExperimentalFeature(ExperimentalFeature missingFeature);
};

/**
 * Semi-magic conversion to and from json.
 * See the nlohmann/json readme for more details.
 */
void to_json(nlohmann::json &, const ExperimentalFeature &);
void from_json(const nlohmann::json &, ExperimentalFeature &);

/**
 * It is always rendered as a string
 */
template<>
struct json_avoids_null<ExperimentalFeature> : std::true_type {};

}
