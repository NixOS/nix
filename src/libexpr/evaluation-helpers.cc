#include "nix/expr/evaluation-helpers.hh"
#include "nix/util/error.hh"
#include "nix/fetchers/fetch-to-store.hh"
#include "nix/expr/attr-path.hh"
#include "nix/expr/eval.hh"

namespace nix::expr::helpers {

bool isDerivation(Object & obj)
{
    auto typeAttr = obj.maybeGetAttr("type");
    if (!typeAttr) {
        return false;
    }

    if (typeAttr->getType() != nString) {
        return false;
    }

    auto typeStr = typeAttr->getStringIgnoreContext();
    return typeStr == "derivation";
}

// FIXME: dedup by removing implementation-specific copies (using `Value` or `AttrCursor`)
StorePath forceDerivation(Evaluator & evaluator, Object & obj, Store & store)
{
    // Get the drvPath attribute from the derivation
    // Source: PackageInfo::requireDrvPath() - all derivations must have a drvPath attribute
    // Error message matches PackageInfo::requireDrvPath() exactly
    auto drvPathAttr = obj.maybeGetAttr("drvPath");
    if (!drvPathAttr) {
        throw Error("derivation does not contain a 'drvPath' attribute");
    }

    // Source: PackageInfo::queryDrvPath() uses coerceToStorePath which preserves context
    // Context is needed because derivation paths reference the Nix store
    auto [drvPathStr, context] = drvPathAttr->getStringWithContext();

    // Source: AttrCursor::forceDerivation() uses parseStorePath for the same purpose
    StorePath drvPath = store.parseStorePath(drvPathStr);

    // Verify that the path is actually a derivation (ends with .drv)
    // Source: PackageInfo::queryDrvPath() - same pattern including try-catch and addTrace
    // This ensures type safety - not all store paths are derivations
    try {
        drvPath.requireDerivation();
    } catch (Error & e) {
        e.addTrace({}, "while evaluating the 'drvPath' attribute of a derivation");
        throw;
    }

    // Check if the derivation exists in the store
    // Source: AttrCursor::forceDerivation() - same logic and error message
    // In read-only mode, skip the check as the store might not be accessible
    // This prevents errors when evaluating derivations without a writable store
    if (!store.isValidPath(drvPath) && !evaluator.isReadOnly()) {
        /* The derivation path has been garbage-collected.
           In the AttrCursor::forceDerivation() version, it tries to regenerate by calling forceValue,
           but we don't have that capability in the Object interface. */
        throw Error("don't know how to recreate store derivation '%s'!", store.printStorePath(drvPath));
    }

    return drvPath;
}

OrSuggestions<std::shared_ptr<Object>> findAlongAttrPath(Object & obj, const std::vector<std::string> & attrPath)
{
    std::shared_ptr<Object> current = obj.shared_from_this();

    for (const auto & attrName : attrPath) {
        auto next = current->maybeGetAttr(attrName);
        if (!next) {
            // Generate suggestions for the missing attribute
            // Source: AttrCursor::getSuggestionsForAttr()
            auto attrNames = current->getAttrNames();
            StringSet strAttrNames;
            for (auto & name : attrNames)
                strAttrNames.insert(name);

            return OrSuggestions<std::shared_ptr<Object>>::failed(Suggestions::bestMatches(strAttrNames, attrName));
        }
        current = next;
    }

    return OrSuggestions<std::shared_ptr<Object>>(current);
}

OrSuggestions<std::pair<std::shared_ptr<Object>, std::string>>
tryAttrPaths(Object & obj, const std::vector<std::string> & attrPaths, EvalState & state)
{
    // Source: InstallableFlake::getCursors() lines 258-284
    // Try each attribute path until one succeeds
    Suggestions suggestions;

    for (auto & attrPath : attrPaths) {
        // Convert Symbol vector to string vector for findAlongAttrPath
        auto attrPathSymbols = parseAttrPath(state, attrPath);
        std::vector<std::string> attrPathStrings;
        for (const auto & sym : attrPathSymbols) {
            attrPathStrings.push_back(std::string(state.symbols[sym]));
        }

        auto objResult = findAlongAttrPath(obj, attrPathStrings);
        if (objResult) {
            return OrSuggestions<std::pair<std::shared_ptr<Object>, std::string>>(std::make_pair(*objResult, attrPath));
        } else {
            suggestions += objResult.getSuggestions();
        }
    }

    return OrSuggestions<std::pair<std::shared_ptr<Object>, std::string>>::failed(suggestions);
}

StringSet getDerivationOutputs(Object & obj)
{
    // Source: InstallableFlake::toDerivedPaths() lines 126-140
    // Logic for determining which outputs to install from a derivation

    StringSet outputsToInstall;

    // Check for outputSpecified attribute (highest priority)
    // Note: The original uses "else if" - if outputSpecified exists (even if false),
    // it won't check meta.outputsToInstall
    if (auto aOutputSpecified = obj.maybeGetAttr("outputSpecified")) {
        if (aOutputSpecified->getBool("while checking outputSpecified")) {
            if (auto aOutputName = obj.maybeGetAttr("outputName")) {
                if (aOutputName->getType() == nString) {
                    auto outputName = aOutputName->getStringIgnoreContext();
                    outputsToInstall = {outputName};
                }
            }
        }
        // If outputSpecified exists but is false, or outputName is missing,
        // outputsToInstall remains empty and we fall through to default
    } else if (auto aMeta = obj.maybeGetAttr("meta")) {
        // Only check meta if outputSpecified attribute doesn't exist
        if (auto aOutputsToInstall = aMeta->maybeGetAttr("outputsToInstall")) {
            for (auto & s : aOutputsToInstall->getListOfStringsNoCtx())
                outputsToInstall.insert(s);
        }
    }

    // Default to "out" if nothing was set
    if (outputsToInstall.empty())
        outputsToInstall.insert("out");

    return outputsToInstall;
}

SingleDerivedPath
coerceToSingleDerivedPathUnchecked(std::string_view str, const NixStringContext & context, std::string_view errorCtx)
{
    auto csize = context.size();
    if (csize != 1)
        throw Error(
            "string '%s' has %d entries in its context. It should only have exactly one entry%s",
            str,
            csize,
            errorCtx.empty() ? "" : std::string(": ") + std::string(errorCtx));

    return std::visit(
        overloaded{
            [&](NixStringContextElem::Opaque && o) -> SingleDerivedPath { return std::move(o); },
            [&](NixStringContextElem::DrvDeep &&) -> SingleDerivedPath {
                throw Error(
                    "string '%s' has a context which refers to a complete source and binary closure. "
                    "This is not supported at this time%s",
                    str,
                    errorCtx.empty() ? "" : std::string(": ") + std::string(errorCtx));
            },
            [&](NixStringContextElem::Built && b) -> SingleDerivedPath { return std::move(b); },
        },
        ((NixStringContextElem &&) *context.begin()).raw);
}

std::optional<DerivedPath> trySinglePathToDerivedPath(Evaluator & evaluator, Object & obj, std::string_view errorCtx)
{
    auto type = obj.getType();

    if (type == nPath) {
        auto storePath =
            fetchToStore(evaluator.getFetchSettings(), evaluator.getStore(), obj.getPath(), FetchMode::Copy);
        return DerivedPath::Opaque{.path = std::move(storePath)};
    }

    else if (type == nString) {
        auto [str, context] = obj.getStringWithContext();
        auto derivedPath = coerceToSingleDerivedPathUnchecked(str, context, errorCtx);
        return DerivedPath::fromSingle(std::move(derivedPath));
    }

    else {
        return std::nullopt;
    }
}

} // namespace nix::expr::helpers