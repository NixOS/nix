#pragma once
///@file

#include "nix/store/build/goal.hh"
#include "nix/store/build/worker.hh"
#include "nix/store/build/substitution-plan-goal.hh"
#include "nix/store/derivations.hh"
#include "nix/store/outputs-spec.hh"
#include "nix/store/realisation.hh"

namespace nix {

/**
 * Decides how one output of a derivation would be obtained: it is
 * valid already, it can be substituted (from where is delegated to a
 * `SubstitutionPlanGoal`), or the derivation has to be built, in which
 * case the plans for its inputs are made too. Nothing is substituted
 * or built; `DerivationGoal` does that, picking up from this plan.
 */
struct DerivationPlanGoal : public Goal
{
    const StorePath drvPath;
    const ref<const Derivation> drv;
    const OutputName wantedOutput;

    struct AlreadyValid
    {
        UnkeyedRealisation realisation;
    };

    struct Substitute
    {
        UnkeyedRealisation realisation;
        std::shared_ptr<SubstitutionPlanGoal> plan;
    };

    struct Build
    {};

    using Plan = std::variant<AlreadyValid, Substitute, Build>;

    /**
     * The plan: set once the goal has finished successfully.
     */
    std::optional<Plan> plan;

    /**
     * When the derivation has to be built, the plans for the outputs of
     * its input derivations.
     */
    Goals inputPlans;

    DerivationPlanGoal(
        const StorePath & drvPath, ref<const Derivation> drv, const OutputName & wantedOutput, Worker & worker);

    std::string key() override;

    JobCategory jobCategory() const override
    {
        return JobCategory::Administration;
    }

private:
    asio::awaitable<ExitCode> init();
};

/**
 * The planning counterpart of `DerivationTrampolineGoal`: obtains the
 * derivation and plans each wanted output. If the derivation is not
 * there yet (it has to be substituted, or built by another
 * derivation), all that can be planned is obtaining it.
 */
struct DerivationTrampolinePlanGoal : public Goal
{
    const ref<const SingleDerivedPath> drvReq;
    const OutputsSpec wantedOutputs;

    /**
     * What the plan consists of: the plans for the wanted outputs, or
     * the plan for obtaining the derivation.
     */
    Goals subPlans;

    DerivationTrampolinePlanGoal(
        ref<const SingleDerivedPath> drvReq, const OutputsSpec & wantedOutputs, Worker & worker);

    std::string key() override;

    JobCategory jobCategory() const override
    {
        return JobCategory::Administration;
    }

private:
    asio::awaitable<ExitCode> init();
};

} // namespace nix
