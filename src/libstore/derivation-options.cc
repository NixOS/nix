#include "derivation-options.hh"
#include "parsed-derivations.hh"

namespace nix {

static std::map<std::string, DerivationOptions::OutputChecks> parseChecksPerOutput(const ParsedDerivation & parsed)
{
    std::map<std::string, DerivationOptions::OutputChecks> res;

    if (auto structuredAttrs = parsed.getStructuredAttrs()) {
        if (auto outputChecks = get(*structuredAttrs, "outputChecks")) {
            for (auto & [outputName, output] : getObject(*outputChecks)) {
                DerivationOptions::OutputChecks checks;

                if (auto maxSize = get(output, "maxSize"))
                    checks.maxSize = maxSize->get<uint64_t>();

                if (auto maxClosureSize = get(output, "maxClosureSize"))
                    checks.maxClosureSize = maxClosureSize->get<uint64_t>();

                auto get_ = [&](const std::string & name) -> std::optional<Strings> {
                    if (auto i = get(output, name)) {
                        Strings res;
                        for (auto j = i->begin(); j != i->end(); ++j) {
                            if (!j->is_string())
                                throw Error("attribute '%s' must be a list of strings", name);
                            res.push_back(j->get<std::string>());
                        }
                        checks.disallowedRequisites = res;
                        return res;
                    }
                    return {};
                };

                checks.allowedReferences = get_("allowedReferences");
                checks.allowedRequisites = get_("allowedRequisites");
                checks.disallowedReferences = get_("disallowedReferences");
                checks.disallowedRequisites = get_("disallowedRequisites");

                res.insert_or_assign(outputName, std::move(checks));
            }
        }
    }

    return res;
}

DerivationOptions DerivationOptions::fromEnv(const StringPairs & env, bool shouldWarn)
{
    ParsedDerivation parsed(env);

    DerivationOptions defaults = {};

    auto structuredAttrs = parsed.getStructuredAttrs();

    if (shouldWarn && structuredAttrs) {
        if (get(*structuredAttrs, "allowedReferences")) {
            warn(
                "'structuredAttrs' disables the effect of the top-level attribute 'allowedReferences'; use 'outputChecks' instead");
        }
        if (get(*structuredAttrs, "allowedRequisites")) {
            warn(
                "'structuredAttrs' disables the effect of the top-level attribute 'allowedRequisites'; use 'outputChecks' instead");
        }
        if (get(*structuredAttrs, "disallowedRequisites")) {
            warn(
                "'structuredAttrs' disables the effect of the top-level attribute 'disallowedRequisites'; use 'outputChecks' instead");
        }
        if (get(*structuredAttrs, "disallowedReferences")) {
            warn(
                "'structuredAttrs' disables the effect of the top-level attribute 'disallowedReferences'; use 'outputChecks' instead");
        }
        if (get(*structuredAttrs, "maxSize")) {
            warn(
                "'structuredAttrs' disables the effect of the top-level attribute 'maxSize'; use 'outputChecks' instead");
        }
        if (get(*structuredAttrs, "maxClosureSize")) {
            warn(
                "'structuredAttrs' disables the effect of the top-level attribute 'maxClosureSize'; use 'outputChecks' instead");
        }
    }

    return {
        .checksPerOutput = parseChecksPerOutput(parsed),
        .checksAllOutputs = parsed.getStructuredAttrs()
                                ? (DerivationOptions::OutputChecks{}) // ignore
                                : (DerivationOptions::OutputChecks{
                                    // legacy non-structured-attributes case
                                    .ignoreSelfRefs = true,
                                    .allowedReferences = parsed.getStringsAttr("allowedReferences"),
                                    .disallowedReferences = parsed.getStringsAttr("disallowedReferences"),
                                    .allowedRequisites = parsed.getStringsAttr("allowedRequisites"),
                                    .disallowedRequisites = parsed.getStringsAttr("disallowedRequisites"),
                                }),
        .additionalSandboxProfile =
            parsed.getStringAttr("__sandboxProfile").value_or(defaults.additionalSandboxProfile),
        .noChroot = parsed.getBoolAttr("__noChroot", defaults.noChroot),
        .impureHostDeps = parsed.getStringsAttr("__impureHostDeps").value_or(defaults.impureHostDeps),
        .impureEnvVars = parsed.getStringsAttr("impureEnvVars").value_or(defaults.impureEnvVars),
        .allowLocalNetworking = parsed.getBoolAttr("__darwinAllowLocalNetworking", defaults.allowLocalNetworking),
        .requiredSystemFeatures =
            parsed.getStringsAttr("requiredSystemFeatures").value_or(defaults.requiredSystemFeatures),
        .preferLocalBuild = parsed.getBoolAttr("preferLocalBuild", defaults.preferLocalBuild),
        .allowSubstitutes = parsed.getBoolAttr("allowSubstitutes", defaults.allowSubstitutes),
    };
}

}
