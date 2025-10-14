#pragma once
/**
 * @file
 * Helper functions for working with the Evaluator interface.
 */

#include "nix/expr/evaluator.hh"
#include "nix/store/store-api.hh"
#include "nix/util/suggestions.hh"
#include "nix/store/derived-path.hh"
#include "nix/fetchers/fetch-settings.hh"

namespace nix::expr::helpers {

/**
 * Check if an Object represents a derivation.
 *
 * A derivation is identified by having a "type" attribute with the value "derivation".
 *
 * @param obj The Object to check
 * @return true if the object is a derivation, false otherwise
 */
bool isDerivation(Object & obj);

/**
 * Force evaluation of a derivation and return its store path.
 * This is only valid for derivations (objects with type = "derivation").
 * Throws an error if this is not a derivation.
 *
 * @param evaluator The Evaluator to check read-only mode
 * @param obj The Object representing the derivation
 * @param store The store to parse the derivation path
 * @return The store path of the derivation
 */
StorePath forceDerivation(Evaluator & evaluator, Object & obj, Store & store);

/**
 * Get an attribute along a chain of attrsets. Note that this does
 * not auto-call functors or functions.
 *
 * @param obj The root object to start navigation from
 * @param attrPath The attribute path to follow (e.g., ["packages", "x86_64-linux", "hello"])
 * @return The object at the end of the path, or Suggestions if not found
 */
OrSuggestions<std::shared_ptr<Object>> findAlongAttrPath(Object & obj, const std::vector<std::string> & attrPath);

/**
 * Try multiple attribute paths and return the first one that succeeds.
 * This is the Object-based version of the logic in InstallableFlake::getCursors().
 *
 * @param obj The root object to start navigation from
 * @param attrPaths List of attribute paths to try (as strings, will be parsed)
 * @param state EvalState for parsing attribute paths
 * @return A pair of (found object, actual path used), or Suggestions if none found
 */
OrSuggestions<std::pair<std::shared_ptr<Object>, std::string>>
tryAttrPaths(Object & obj, const std::vector<std::string> & attrPaths, EvalState & state);

/**
 * Get the outputs to install for a derivation based on its metadata.
 * This implements the logic from InstallableFlake::toDerivedPaths for determining
 * which outputs should be installed from a derivation.
 *
 * The logic follows this priority:
 * 1. If outputSpecified = true, use outputName attribute
 * 2. Otherwise, if meta.outputsToInstall exists, use that list
 * 3. Otherwise, default to ["out"]
 *
 * @param obj The Object representing a derivation
 * @return Set of output names to install
 */
StringSet getDerivationOutputs(Object & obj);

/**
 * Coerce a string with context to a SingleDerivedPath.
 * This is the Object-based version of the core logic in EvalState::coerceToSingleDerivedPathUnchecked.
 *
 * The string must have exactly one context element, which can be:
 * - Opaque: A direct store path reference
 * - Built: A derivation output reference
 * - DrvDeep: Not supported (throws error)
 *
 * @param str The string value (for error messages)
 * @param context The string's context (must have exactly one element)
 * @param errorCtx Context for error messages
 * @return The SingleDerivedPath extracted from the context
 * @throws Error if context size != 1 or if context is DrvDeep
 */
SingleDerivedPath
coerceToSingleDerivedPathUnchecked(std::string_view str, const NixStringContext & context, std::string_view errorCtx);

/**
 * Try to convert a single path or string value to a DerivedPath.
 * This is the Object-based version of the core logic in InstallableValue::trySinglePathToDerivedPaths.
 *
 * Handles two cases:
 * - nPath: Fetches the path to the store and returns an Opaque derived path
 * - nString: Coerces to a SingleDerivedPath using the string's context
 *
 * @param evaluator The Evaluator (provides store and fetch settings)
 * @param obj The Object to try converting
 * @param errorCtx Context for error messages
 * @return A DerivedPath if the object is a path or string, nullopt otherwise
 */
std::optional<DerivedPath> trySinglePathToDerivedPath(Evaluator & evaluator, Object & obj, std::string_view errorCtx);

} // namespace nix::expr::helpers