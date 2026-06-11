#pragma once
/**
 * @file
 *
 * @brief Thread-local evaluation context for enriching error messages.
 *
 * Provides a mechanism to annotate errors with the high-level user action
 * that triggered them (e.g. "during evaluation of installable nixpkgs#hello").
 */

#include <optional>
#include <string>

namespace nix {

/**
 * Return the current thread-local evaluation context string, if any.
 */
const std::optional<std::string> & currentEvalContext();

/**
 * RAII guard that sets a thread-local evaluation context string.
 * When a BaseError is constructed while a guard is active, the context
 * is automatically stamped onto ErrorInfo::evalContext.
 *
 * Only the outermost guard takes effect — nested guards are no-ops,
 * so the context always reflects the top-level user action.
 *
 * Usage:
 *   EvalContextGuard ctx("evaluation of installable nixpkgs#hello");
 *   // ... any BaseError thrown here will carry the context ...
 */
class EvalContextGuard
{
    std::optional<std::string> previous;
    bool didSet;
public:
    explicit EvalContextGuard(std::string context);
    ~EvalContextGuard();
    EvalContextGuard(const EvalContextGuard &) = delete;
    EvalContextGuard & operator=(const EvalContextGuard &) = delete;
};

}
