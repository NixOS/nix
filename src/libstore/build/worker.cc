#include "nix/store/local-store.hh"
#include "nix/store/machines.hh"
#include "nix/store/store-open.hh"
#include "nix/store/build/worker.hh"
#include "nix/store/build/substitution-goal.hh"
#include "nix/store/build/drv-output-substitution-goal.hh"
#include "nix/store/build/derivation-goal.hh"
#include "nix/store/build/derivation-resolution-goal.hh"
#include "nix/store/build/derivation-building-goal.hh"
#include "nix/store/build/derivation-trampoline-goal.hh"
#ifndef _WIN32 // TODO Enable building on Windows
#  include "nix/store/build/hook-instance.hh"
#endif
#include "nix/util/signals.hh"
#include "nix/store/globals.hh"

#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/signal_set.hpp>

namespace nix {

/* FIXME: Dedup a lot. */
static bool isCancellationException(std::exception_ptr ex)
{
    try {
        std::rethrow_exception(ex);
    } catch (const Interrupted &) {
        return true;
    } catch (const Cancelled &) {
        return true;
    } catch (const boost::system::system_error & e) {
        return e.code() == asio::error::operation_aborted;
    } catch (...) {
        return false;
    }
}

Worker::Worker(Store & store, Store & evalStore)
    /* Can't use make_ref, because the constructor is private. */
    : settings(nix::settings.getWorkerSettings())
    , buildSemaphore(ex, settings.maxBuildJobs)
    , substitutionSemaphore(ex, std::max<std::size_t>(settings.maxSubstitutionJobs, 1))
    , act(*logger, actRealise)
    , actDerivations(*logger, actBuilds)
    , actSubstitutions(*logger, actCopyPaths)
    , store(store)
    , evalStore(evalStore)
    , getSubstituters{[] {
        return nix::settings.getWorkerSettings().useSubstitutes ? getDefaultSubstituters() : std::list<ref<Store>>{};
    }}
{
}

Worker::~Worker()
{
    /* Explicitly get rid of all strong pointers now.  After this all
       goals that refer to this worker should be gone.  (Otherwise we
       are in trouble, since goals may call childTerminated() etc. in
       their destructors). */
    topGoals.clear();

    assert(expectedSubstitutions == 0);
    assert(expectedDownloadSize == 0);
    assert(expectedNarSize == 0);
}

template<class G, typename... Args>
std::shared_ptr<G> Worker::initGoalIfNeeded(std::weak_ptr<G> & goal_weak, Args &&... args)
{
    if (auto goal = goal_weak.lock())
        return goal;

    auto goal = std::make_shared<G>(std::forward<Args>(args)...);
    goal_weak = goal;
    return goal;
}

std::shared_ptr<DerivationTrampolineGoal> Worker::makeDerivationTrampolineGoal(
    ref<const SingleDerivedPath> drvReq, const OutputsSpec & wantedOutputs, BuildMode buildMode)
{
    return initGoalIfNeeded(
        derivationTrampolineGoals.ensureSlot(*drvReq).value[wantedOutputs], drvReq, wantedOutputs, *this, buildMode);
}

std::shared_ptr<DerivationTrampolineGoal> Worker::makeDerivationTrampolineGoal(
    const StorePath & drvPath, const OutputsSpec & wantedOutputs, const Derivation & drv, BuildMode buildMode)
{
    return initGoalIfNeeded(
        derivationTrampolineGoals.ensureSlot(DerivedPath::Opaque{drvPath}).value[wantedOutputs],
        drvPath,
        wantedOutputs,
        drv,
        *this,
        buildMode);
}

std::shared_ptr<DerivationGoal> Worker::makeDerivationGoal(
    const StorePath & drvPath,
    ref<const Derivation> drv,
    const OutputName & wantedOutput,
    BuildMode buildMode,
    bool storeDerivation)
{
    return initGoalIfNeeded(
        derivationGoals[drvPath][wantedOutput],
        drvPath,
        std::move(drv),
        wantedOutput,
        *this,
        buildMode,
        storeDerivation);
}

std::shared_ptr<DerivationResolutionGoal>
Worker::makeDerivationResolutionGoal(const StorePath & drvPath, ref<const Derivation> drv, BuildMode buildMode)
{
    return initGoalIfNeeded(derivationResolutionGoals[drvPath], drvPath, drv, *this, buildMode);
}

std::shared_ptr<DerivationBuildingGoal>
Worker::makeDerivationBuildingGoal(const StorePath & drvPath, ref<const BasicDerivation> drv, BuildMode buildMode)
{
    return initGoalIfNeeded(derivationBuildingGoals[drvPath], drvPath, std::move(drv), *this, buildMode);
}

std::shared_ptr<PathSubstitutionGoal>
Worker::makePathSubstitutionGoal(const StorePath & path, RepairFlag repair, std::optional<ContentAddress> ca)
{
    return initGoalIfNeeded(substitutionGoals[path], path, *this, repair, ca);
}

std::shared_ptr<DrvOutputSubstitutionGoal> Worker::makeDrvOutputSubstitutionGoal(const DrvOutput & id)
{
    return initGoalIfNeeded(drvOutputSubstitutionGoals[id], id, *this);
}

GoalPtr Worker::makeGoal(const DerivedPath & req, BuildMode buildMode)
{
    return std::visit(
        overloaded{
            [&](const DerivedPath::Built & bfd) -> GoalPtr {
                return makeDerivationTrampolineGoal(bfd.drvPath, bfd.outputs, buildMode);
            },
            [&](const DerivedPath::Opaque & bo) -> GoalPtr {
                return makePathSubstitutionGoal(bo.path, buildMode == bmRepair ? Repair : NoRepair);
            },
        },
        req.raw());
}

void Worker::removeGoal(GoalPtr goal)
{
    if (auto drvGoal = std::dynamic_pointer_cast<DerivationTrampolineGoal>(goal)) {
        derivationTrampolineGoals.removeSlot(*drvGoal->drvReq, [&](auto & node) {
            node.value.erase(drvGoal->wantedOutputs);
            /* Return true if ancestors don't need to be pruned. */
            return !node.value.empty();
        });
    } else if (auto drvGoal = std::dynamic_pointer_cast<DerivationGoal>(goal)) {
        if (auto it = derivationGoals.find(drvGoal->drvPath); it != derivationGoals.end()) {
            it->second.erase(drvGoal->wantedOutput);
            if (it->second.empty())
                derivationGoals.erase(it);
        }
    } else if (auto drvResolutionGoal = std::dynamic_pointer_cast<DerivationResolutionGoal>(goal)) {
        derivationResolutionGoals.erase(drvResolutionGoal->drvPath);
    } else if (auto drvBuildingGoal = std::dynamic_pointer_cast<DerivationBuildingGoal>(goal)) {
        derivationBuildingGoals.erase(drvBuildingGoal->drvPath);
    } else if (auto subGoal = std::dynamic_pointer_cast<PathSubstitutionGoal>(goal)) {
        substitutionGoals.erase(subGoal->getStorePath());
    } else if (auto subGoal = std::dynamic_pointer_cast<DrvOutputSubstitutionGoal>(goal)) {
        drvOutputSubstitutionGoals.erase(subGoal->id);
    } else {
        unreachable();
    }

    if (topGoals.find(goal) != topGoals.end()) {
        topGoals.erase(goal);
        /* If a top-level goal failed, then kill all other goals
           (unless keepGoing was set). */
        if (goal->exitCode == Goal::ecFailed && !settings.keepGoing)
            topGoals.clear();
    }
}

asio::awaitable<void> Worker::awaitTopGoals()
{
    co_await Goal::join(topGoals, settings.keepGoing);
}

void Worker::run(const Goals & _topGoals)
{
    /* The same worker may be run more than once (see `repairPath`), and
       a stopped `io_context` must be restarted before it does work again. */
    ioContext.restart();

    for (auto & goal : _topGoals)
        topGoals.insert(goal);

    std::exception_ptr runError;
    asio::cancellation_signal interrupted;

    auto callback = createInterruptCallback(
        [&]() { asio::post(ex, [&interrupted] { interrupted.emit(asio::cancellation_type::terminal); }); });

    /* Periodically give the local store a chance to collect garbage while
       builds are running (see `min-free`). Stops once the top-level goals
       are done. */
    asio::cancellation_signal stopBackground;
    asio::co_spawn(
        ex,
        [this]() -> asio::awaitable<void> {
            // TODO GC interface?
            auto localStore = dynamic_cast<LocalStore *>(&store);
            if (!localStore)
                co_return;
            asio::steady_timer timer(co_await asio::this_coro::executor);
            while (true) {
                localStore->autoGC(false);
                timer.expires_after(std::chrono::seconds(10));
                co_await timer.async_wait(asio::use_awaitable);
            }
        },
        asio::bind_cancellation_slot(stopBackground.slot(), asio::detached));

    asio::co_spawn(ex, awaitTopGoals(), asio::bind_cancellation_slot(interrupted.slot(), [&](std::exception_ptr e) {
                       if (e && !isCancellationException(e))
                           runError = e;
                       stopBackground.emit(asio::cancellation_type::terminal);
                   }));

    ioContext.run();

    checkInterrupt();
    if (runError)
        std::rethrow_exception(runError);
}

bool Worker::pathContentsGood(const StorePath & path)
{
    auto i = pathContentsGoodCache.find(path);
    if (i != pathContentsGoodCache.end())
        return i->second;
    printInfo("checking path '%s'...", store.printStorePath(path));
    auto info = store.queryPathInfo(path);
    bool res = false;
    if (auto accessor = store.getFSAccessor(path, /*requireValidPath=*/false)) {
        auto current = hashPath({ref{accessor}}, FileIngestionMethod::NixArchive, info->narHash.algo).first;
        Hash nullHash(HashAlgorithm::SHA256);
        res = info->narHash == nullHash || info->narHash == current;
    }
    pathContentsGoodCache.insert_or_assign(path, res);
    if (!res)
        printError("path '%s' is corrupted or missing!", store.printStorePath(path));
    return res;
}

void Worker::markContentsGood(const StorePath & path)
{
    pathContentsGoodCache.insert_or_assign(path, true);
}

GoalPtr upcast_goal(std::shared_ptr<PathSubstitutionGoal> subGoal)
{
    return subGoal;
}

GoalPtr upcast_goal(std::shared_ptr<DrvOutputSubstitutionGoal> subGoal)
{
    return subGoal;
}

GoalPtr upcast_goal(std::shared_ptr<DerivationGoal> subGoal)
{
    return subGoal;
}

} // namespace nix
