#pragma once
///@file

#include "nix/store/derivations.hh"
#include "nix/store/derivation-options.hh"
#include "nix/store/build/derivation-building-misc.hh"
#include "nix/store/store-api.hh"
#include "nix/store/build/goal.hh"

namespace nix {

struct BuilderFailureError;

/**
 * A goal for resolving a derivation. Resolving a derivation (@see
 * `Derivation::tryResolve`) simplifies its inputs, replacing
 * `inputDrvs` with `inputSrcs`.
 *
 * Conceptually, we resolve all derivations. For input-addressed
 * derivations (that don't transtively depend on content-addressed
 * derivations), however, we don't actually use the resolved derivation,
 * because the output paths would appear invalid (if we tried to verify
 * them), since they are computed from the original, unresolved inputs.
 *
 * That said, if we ever made the new flavor of input-addressing as described
 * in issue #9259, then the input-addressing would be based on the resolved
 * inputs, and we like the CA case *would* use the output of this goal.
 *
 * (The point of this discussion is not to randomly stuff information on
 * a yet-unimplemented feature (issue #9259) in the codebase, but
 * rather, to illustrate that there is no inherent tension between
 * explicit derivation resolution and input-addressing in general. That
 * tension only exists with the type of input-addressing we've
 * historically used.)
 */
struct DerivationResolutionGoal : public Goal
{
    DerivationResolutionGoal(const StorePath & drvPath, const Derivation & drv, Worker & worker, BuildMode buildMode);

    /**
     * If the derivation needed to be resolved, this is resulting
     * resolved derivations and its path.
     */
    std::unique_ptr<std::pair<StorePath, BasicDerivation>> resolvedDrv;

    void timedOut(Error && ex) override {}

private:

    /**
     * The path of the derivation.
     */
    StorePath drvPath;

    /**
     * The derivation stored at drvPath.
     */
    std::unique_ptr<Derivation> drv;

    /**
     * The remainder is state held during the build.
     */

    BuildMode buildMode;

    std::unique_ptr<Activity> act;

    std::string key() override;

    /**
     * The states.
     */
    Co resolveDerivation();

    JobCategory jobCategory() const override
    {
        return JobCategory::Administration;
    };
};

} // namespace nix
