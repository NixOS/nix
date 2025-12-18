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

    // ASSUME WE CANNOT FETCH REALISTION

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
