#include "nix/store/build/derived-output-goal.hh"
#include "nix/store/build/build-trace-trampoline-goal.hh"
#include "nix/store/build/worker.hh"
#include "nix/util/util.hh"

namespace nix {

DerivedOutputGoal::DerivedOutputGoal(const SingleDerivedPath::Built & id, Worker & worker, BuildMode buildMode)
    : Goal{worker, init()}
    , id{id}
    , buildMode{buildMode}
{
    name = fmt("getting derived output '%s'", id.to_string(worker.store));
    trace("created");
}

static StorePath pathPartOfReq(const SingleDerivedPath & req)
{
    return std::visit(
        overloaded{
            [&](const SingleDerivedPath::Opaque & bo) { return bo.path; },
            [&](const SingleDerivedPath::Built & bfd) { return pathPartOfReq(*bfd.drvPath); },
        },
        req.raw());
}

std::string DerivedOutputGoal::key()
{
    return "do$" + std::string(pathPartOfReq(*id.drvPath).name()) + "$" + id.to_string(worker.store);
}

Goal::Co DerivedOutputGoal::init()
{
    trace("init");

    /* Check if this is an input-addressed derivation with a statically-known
       output path. If so, skip build trace lookup and build directly. */
    bool usesBuildTrace = true;
    if (auto * opaque = std::get_if<SingleDerivedPath::Opaque>(&*id.drvPath)) {
        for (auto * drvStore : {&worker.evalStore, &worker.store}) {
            if (drvStore->isValidPath(opaque->path)) {
                auto drv = drvStore->readDerivation(opaque->path);
                auto outputs = drv.outputsAndOptPaths(worker.store);
                if (auto * output = get(outputs, id.output)) {
                    if (output->second) {
                        /* Output path is statically known (input-addressed).
                           Check if it already exists. */
                        if (worker.store.isValidPath(*output->second)) {
                            trace("output already exists");
                            outputPath = *output->second;
                            co_return amDone(ecSuccess);
                        }
                        /* Input-addressed, but output doesn't exist. Build it. */
                        trace("input-addressed derivation, skipping build trace");
                        usesBuildTrace = false;
                    }
                }
                break;
            }
        }
    }

    if (usesBuildTrace) {
        /* Try to look up the realisation via BuildTraceTrampolineGoal. */
        auto btGoal = worker.makeBuildTraceTrampolineGoal(id);

        co_await await(Goals{upcast_goal(btGoal)});

        if (btGoal->outputInfo) {
            /* Found a realisation! Check if the output is available. */
            trace("found realisation via build trace lookup");
            outputPath = btGoal->outputInfo->outPath;

            /* Check if the output path exists locally or in any substitutor. */
            if (worker.store.isValidPath(*outputPath)) {
                trace("realisation found, and output is known to exist in default store");
                co_return amDone(ecSuccess);
            }

            for (auto & sub : worker.getSubstituters()) {
                if (sub->isValidPath(*outputPath)) {
                    trace(
                        fmt("realisation found, and output is known to exist in substitutor '%s'",
                            sub->config.getHumanReadableURI()));
                    co_return amDone(ecSuccess);
                }
            }

            trace("realisation found but output not available, falling back to building");
        } else {
            trace("no realisation found, falling back to building");
        }
    }

    /* Reset counters since we're starting a fresh build attempt. */
    nrFailed = 0;
    nrNoSubstituters = 0;

    /* No realisation found. Fall back to building via DerivationGoal.
       We use makeGoal which will create a DerivationTrampolineGoal,
       which handles getting the derivation and building it. */
    auto buildGoal = worker.makeGoal(
        DerivedPath::Built{
            .drvPath = id.drvPath,
            .outputs = OutputsSpec::Names{id.output},
        },
        buildMode);

    buildGoal->preserveFailure = true;

    co_await await(Goals{buildGoal});

    trace("build goal finished");

    /* Extract the output path from the build result. */
    if (auto * successP = buildGoal->buildResult.tryGetSuccess()) {
        if (auto * realisation = get(successP->builtOutputs, id.output)) {
            outputPath = realisation->outPath;
        }
    }

    co_return amDone(buildGoal->exitCode);
}

} // namespace nix
