#pragma once

#include "comparator.hh"
#include "error.hh"
#include "nlohmann/json_fwd.hpp"
#include "types.hh"

namespace nix {

/**
 * The list of available experimental features.
 *
 * If you update this, don’t forget to also change the map defining their
 * string representation in the corresponding `.cc` file.
 **/
enum struct ExperimentalFeature
{
    CaDerivations,
    Flakes,
    NixCommand,
    RecursiveNix,
    NoUrlLiterals,
    ExternalGCDaemon,
};

/**
 * Just because writing `ExperimentalFeature::CaDerivations` is way too long
 */
using Xp = ExperimentalFeature;

const std::optional<ExperimentalFeature> parseExperimentalFeature(
        const std::string_view & name);
std::string_view showExperimentalFeature(const ExperimentalFeature);

std::ostream & operator<<(
        std::ostream & str,
        const ExperimentalFeature & feature);

/**
 * Parse a set of strings to the corresponding set of experimental features,
 * ignoring (but warning for) any unkwown feature.
 */
std::set<ExperimentalFeature> parseFeatures(const std::set<std::string> &);

class MissingExperimentalFeature : public Error
{
public:
    ExperimentalFeature missingFeature;

    MissingExperimentalFeature(ExperimentalFeature);
    virtual const char * sname() const override
    {
        return "MissingExperimentalFeature";
    }
};

}
