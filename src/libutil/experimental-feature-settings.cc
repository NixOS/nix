#include "nix/util/experimental-feature-settings.hh"

namespace nix {

const extern ExperimentalFeatureSettings experimentalFeatureSettingsDefaults = {{
    .experimentalFeatures =
        {
            .value = {},
        },
}};

ExperimentalFeatureSettings experimentalFeatureSettings = experimentalFeatureSettingsDefaults;

bool ExperimentalFeatureSettings::isEnabled(const ExperimentalFeature & feature) const
{
    auto & f = experimentalFeatures.get();
    return std::find(f.begin(), f.end(), feature) != f.end();
}

void ExperimentalFeatureSettings::require(const ExperimentalFeature & feature, std::string reason) const
{
    if (!isEnabled(feature))
        throw MissingExperimentalFeature(feature, std::move(reason));
}

bool ExperimentalFeatureSettings::isEnabled(const std::optional<ExperimentalFeature> & feature) const
{
    return !feature || isEnabled(*feature);
}

void ExperimentalFeatureSettings::require(const std::optional<ExperimentalFeature> & feature) const
{
    if (feature)
        require(*feature);
}

}; // namespace nix
