#include "parse-derivation-common.hh"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size)
{
    using namespace nix;

    /* TODO: The corpus needs CA and Impure drvs. */
    experimentalFeatureSettings.experimentalFeatures.get() = std::set<ExperimentalFeature>{
        Xp::DynamicDerivations,
        Xp::CaDerivations,
        Xp::ImpureDerivations,
    };

    return fuzzParseDerivationCommon(
        std::string_view(reinterpret_cast<const char *>(data), reinterpret_cast<const char *>(data) + size));
}
