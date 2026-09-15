#include "nix/store/build/derivation-plan-goal.hh"
#include "nix/store/build/derivation-goal.hh"
#include "nix/store/build/drv-output-substitution-goal.hh"
#include "nix/store/build/worker.hh"
#include "nix/store/derivation-options.hh"
#include "nix/store/outputs-query.hh"

namespace nix {

DerivationPlanGoal::DerivationPlanGoal(
    const StorePath & drvPath, ref<const Derivation> drv, const OutputName & wantedOutput, Worker & worker)
    : Goal(worker, init())
    , drvPath(drvPath)
    , drv(std::move(drv))
    , wantedOutput(wantedOutput)
{
    name = fmt("planning output '%s' of derivation '%s'", wantedOutput, worker.store.printStorePath(drvPath));
    trace("created");
}

std::string DerivationPlanGoal::key()
{
    return "dbp$" + std::string(drvPath.name()) + "$" + SingleDerivedPath::Built{
        .drvPath = makeConstantStorePathRef(drvPath),
        .output = wantedOutput,
    }.to_string(worker.store);
}

asio::awaitable<Goal::ExitCode> DerivationPlanGoal::init()
{
    trace("init");

    auto drvOptions = [&]() -> DerivationOptions<SingleDerivedPath> {
        try {
            return derivationOptionsFromStructuredAttrs(worker.store, drv->inputs, drv->env, get(drv->structuredAttrs));
        } catch (Error & e) {
            e.addTrace({}, "while parsing derivation '%s'", worker.store.printStorePath(drvPath));
            throw;
        }
    }();

    if (!type(*drv).hasKnownOutputPaths())
        experimentalFeatureSettings.require(Xp::CaDerivations);

    /* An impure derivation is always built. */
    if (!type(*drv).isImpure()) {
        auto checkResult = checkPathValidity(worker, drvPath, *drv, wantedOutput, bmNormal);

        if (checkResult && checkResult->second == PathStatus::Valid) {
            plan = AlreadyValid{checkResult->first};
            co_return ecSuccess;
        }

        /* Can the output be substituted? For a floating output, first
           find out from the substituters what the output path is. */
        if (worker.settings.useSubstitutes && drvOptions.substitutesAllowed(worker.settings)) {
            std::optional<UnkeyedRealisation> realisation;

            if (checkResult)
                realisation = checkResult->first;
            else {
                auto g = worker.makeDrvOutputSubstitutionGoal({drvPath, wantedOutput});
                co_await await({g});
                if (nrFailed == 0)
                    realisation = *g->outputInfo;
                nrFailed = nrNoSubstituters = 0;
            }

            if (realisation) {
                auto * cap = getDerivationCA(*drv);
                auto subPlan =
                    worker.makeSubstitutionPlanGoal(realisation->outPath, cap ? std::optional{*cap} : std::nullopt);
                co_await await({subPlan});
                if (nrFailed == 0) {
                    plan = Substitute{std::move(*realisation), std::move(subPlan)};
                    co_return ecSuccess;
                }
                nrFailed = nrNoSubstituters = 0;
            }
        }
    }

    /* The derivation has to be built, which takes its inputs. */
    for (const auto & input : drv->inputs)
        if (auto * built = std::get_if<SingleDerivedPath::Built>(&input.raw()))
            inputPlans.insert(worker.makePlanGoal(
                DerivedPath::Built{
                    .drvPath = built->drvPath,
                    .outputs = OutputsSpec::Names{built->output},
                }));

    co_await await(inputPlans);

    trace("planned");

    plan = Build{};
    co_return ecSuccess;
}

DerivationTrampolinePlanGoal::DerivationTrampolinePlanGoal(
    ref<const SingleDerivedPath> drvReq, const OutputsSpec & wantedOutputs, Worker & worker)
    : Goal(worker, init())
    , drvReq(std::move(drvReq))
    , wantedOutputs(wantedOutputs)
{
    name = fmt("planning outputs %s of '%s'", wantedOutputs.to_string(), this->drvReq->to_string(worker.store));
    trace("created");
}

std::string DerivationTrampolinePlanGoal::key()
{
    return "dap$" + std::string(drvReq->getBaseStorePath().name()) + "$" + DerivedPath::Built{
        .drvPath = drvReq,
        .outputs = wantedOutputs,
    }.to_string(worker.store);
}

asio::awaitable<Goal::ExitCode> DerivationTrampolinePlanGoal::init()
{
    trace("init");

    auto optDrvPath = [&]() -> std::optional<StorePath> {
        try {
            auto drvPath = resolveDerivedPath(worker.store, *drvReq);
            if (worker.evalStore.isValidPath(drvPath) || worker.store.isValidPath(drvPath))
                return drvPath;
        } catch (MissingRealisation &) {
        }
        return std::nullopt;
    }();

    if (!optDrvPath) {
        /* The derivation has to be obtained first: substituted, or
           built by another derivation. What building it entails cannot
           be known until then, so that is as far as this plan goes. */
        trace("derivation not there yet, planning to obtain it");
        subPlans.insert(worker.makePlanGoal(DerivedPath::fromSingle(*drvReq)));
    } else {
        auto & drvPath = *optDrvPath;

        auto drv = make_ref<const Derivation>([&] {
            for (auto * drvStore : {&worker.evalStore, &worker.store})
                if (drvStore->isValidPath(drvPath))
                    return drvStore->readDerivation(drvPath);
            unreachable();
        }());

        auto resolvedWantedOutputs = std::visit(
            overloaded{
                [&](const OutputsSpec::Names & names) -> OutputsSpec::Names { return names; },
                [&](const OutputsSpec::All &) -> OutputsSpec::Names {
                    StringSet outputs;
                    for (auto & [outputName, _] : drv->outputs)
                        outputs.insert(outputName);
                    return outputs;
                },
            },
            wantedOutputs.raw);

        for (auto & output : resolvedWantedOutputs)
            subPlans.insert(worker.makeDerivationPlanGoal(drvPath, drv, output));
    }

    co_await await(subPlans);

    trace("planned");

    co_return nrFailed == 0 ? ecSuccess : ecFailed;
}

} // namespace nix
