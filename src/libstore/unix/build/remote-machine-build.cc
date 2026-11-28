#include "nix/store/build/remote-machine-build.hh"
#include "nix/store/build.hh"
#include "nix/store/globals.hh"
#include "nix/store/local-store.hh"
#include "nix/store/remote-store.hh"
#include "nix/store/ssh-store.hh"
#include "nix/store/worker-protocol.hh"
#include "nix/store/store-open.hh"
#include "nix/util/experimental-features.hh"
#include "nix/util/finally.hh"
#include "nix/util/util.hh"

#include <boost/asio/steady_timer.hpp>

#include <array>
#include <optional>
#include <ranges>
#include <thread>

#include <unistd.h>

namespace nix {

static ref<Store> openMachineStore(
    const RemoteMachineBuildRequest & req, const Machine & machine, asio::io_context & ctx, asio::yield_context yield)
{
    auto storeUri = machine.completeStoreReference();
    /* `log-fd` would be the hook's descriptor 4, which does not exist here. */
    storeUri.params.erase("log-fd");

    auto config = resolveStoreConfig(std::move(storeUri));
    auto sshConfig = config.dynamic_pointer_cast<const SSHStoreConfig>();
    if (!sshConfig || config.dynamic_pointer_cast<const MountedSSHStoreConfig>())
        throw Error("remote builder '%s' is not an ssh-ng store", machine.storeUri.render());
    return sshConfig->openStoreAsync(ctx, yield, req.messageLogger);
}

bool machineRunsInProcess(const Machine & machine)
{
    try {
        auto config = resolveStoreConfig(machine.completeStoreReference());
        return !config.dynamic_pointer_cast<const MountedSSHStoreConfig>()
               && config.dynamic_pointer_cast<const SSHStoreConfig>();
    } catch (Error &) {
        /* Let the hook report it. */
        return false;
    }
}

static void
acquireUploadLock(Descriptor lock, std::string_view storeUri, asio::io_context & ctx, asio::yield_context yield)
{
    Activity act(*logger, lvlTalkative, actUnknown, fmt("waiting for the upload lock to '%s'", storeUri));

    auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(15);
    while (!lockFile(lock, ltWrite, false)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            printError("somebody is hogging the upload lock for '%s', continuing...", storeUri);
            return;
        }
        asio::steady_timer timer(ctx, std::chrono::seconds(1));
        boost::system::error_code ec;
        {
            SuspendedActivity suspended;
            timer.async_wait(yield[ec]);
        }
        if (ec)
            throw SystemError(ec, "waiting for the upload lock to '%s'", storeUri);
    }
}

RemoteMachineBuildOutcome buildOnRemoteMachine(
    Store & store,
    Machines & machines,
    AcquiredMachine acquired,
    const RemoteMachineBuildRequest & req,
    asio::io_context & ctx,
    asio::yield_context yield)
{
    if (yield.cancelled() != asio::cancellation_type::none)
        throw SystemError(boost::system::error_code(asio::error::operation_aborted), "remote build cancelled");

    /* A terminal cancellation reaches only the pending operation. Make later ones (a destructor
       flushing while unwinding) fail at once, and turn this off before the coroutine completes. */
    yield.throw_if_cancelled(true);
    Finally noLongerThrowIfCancelled([&] { yield.throw_if_cancelled(false); });

    auto currentLoad = getCurrentLoadDir(store);

    /* Skip an unreachable machine for this build only. One refused connection (an sshd at
       `MaxStartups`) must not take it out of service for the rest of the invocation. */
    std::shared_ptr<Store> sshStore;
    std::string storeUri;
    auto unreachableOwned = req.unreachable ? req.unreachable : std::make_shared<std::set<const Machine *>>();
    auto & unreachable = *unreachableOwned;
    bool failedOver = false;
    while (true) {
        storeUri = acquired.machine->storeUri.render();
        try {
            Activity act(*logger, lvlTalkative, actUnknown, fmt("connecting to '%s'", storeUri));
            sshStore = openMachineStore(req, *acquired.machine, ctx, yield);
            sshStore->connect();
            break;
        } catch (Interrupted &) {
            throw;
        } catch (std::exception & e) {
            if (yield.cancelled() != asio::cancellation_type::none)
                throw;
            printError("cannot build on '%s': %s", storeUri, e.what());
            unreachable.insert(acquired.machine);
            failedOver = true;
        }

        acquired.slotLock.close();
        auto selection = acquireMachineSlot(currentLoad, machines, req.neededSystem, req.requiredFeatures, unreachable);
        if (auto * noMachine = std::get_if<NoMachine>(&selection))
            return RemoteMachineUnavailable{*noMachine};
        acquired = std::get<AcquiredMachine>(std::move(selection));
        if (!machineRunsInProcess(*acquired.machine))
            return RemoteMachineUnavailable{NoMachine::AllBusy};
    }

    if (failedOver)
        printInfo("building '%s' on '%s' instead", store.printStorePath(req.drvPath), storeUri);
    if (req.onMachine)
        req.onMachine(storeUri);

    AutoCloseFD uploadLock = openUploadLock(currentLoad, storeUri);
    acquireUploadLock(uploadLock.get(), storeUri, ctx, yield);

    auto substitute = settings.getWorkerSettings().buildersUseSubstitutes ? Substitute : NoSubstitute;

    /* The connection must stay on this thread. `RemoteStore::addMultipleToStore` does so only for
       a daemon of protocol 1.32 or newer. */
    auto copyToMachine = [&](const StorePathSet & paths) {
        auto * remote = dynamic_cast<RemoteStore *>(&*sshStore);
        if (remote
            && WorkerProto::Version::Number::fromWire(remote->getProtocol()) < WorkerProto::Version::Number{1, 32})
            for (auto & path : store.topoSortPaths(paths) | std::views::reverse)
                copyStorePath(store, *sshStore, path, NoRepair, NoCheckSigs);
        else
            copyPaths(store, *sshStore, paths, NoRepair, NoCheckSigs, substitute);
    };

    {
        Activity act(*logger, lvlTalkative, actUnknown, fmt("copying dependencies to '%s'", storeUri));
        copyToMachine(req.inputPaths);
    }

    uploadLock.close();

    auto & drv = *req.drv;

    BuildResult result;

    // If we don't know whether we are trusted (e.g. `ssh://`
    // stores), we assume we are. This is necessary for backwards
    // compat.
    bool trustedOrLegacy = ({
        std::optional trusted = sshStore->isTrustedClient();
        !trusted || *trusted;
    });

    // See the very large comment in `case WorkerProto::Op::BuildDerivation:` in
    // `src/libstore/daemon.cc` that explains the trust model here.
    if (trustedOrLegacy || type(drv).isCA()) {
        result = sshStore->getBuilder()->buildDerivation(req.drvPath, drv);
    } else {
        StorePathSet closure;
        store.computeFSClosure(StorePathSet{req.drvPath}, closure);
        copyToMachine(closure);
        auto res = sshStore->getBuilder()->buildPathsWithResults({DerivedPath::Built{
            .drvPath = makeConstantStorePathRef(req.drvPath),
            .outputs = OutputsSpec::All{},
        }});
        assert(res.size() == 1);
        result = std::move(res[0]);
    }

    if (result.tryGetFailure()) {
        if (settings.keepFailed)
            warn(
                "The failed build directory was kept on the remote builder due to `--keep-failed`.%s",
                (settings.thisSystem == drv.platform || settings.extraPlatforms.get().count(drv.platform) > 0)
                    ? " You can re-run the command with `--builders ''` to disable remote building for this invocation."
                    : "");
        return RemoteMachineBuilt{storeUri, std::move(result)};
    }

    std::set<Realisation> missingRealisations;
    StorePathSet missingPaths;
    if (experimentalFeatureSettings.isEnabled(Xp::CaDerivations) && !type(drv).hasKnownOutputPaths()) {
        for (auto & outputName : req.wantedOutputs) {
            auto thisOutputId = DrvOutput{req.drvPath, outputName};
            if (!store.queryRealisation(thisOutputId)) {
                debug("missing output %s", outputName);
                auto * success = result.tryGetSuccess();
                assert(success);
                auto i = success->builtOutputs.find(outputName);
                assert(i != success->builtOutputs.end());
                auto & newRealisation = i->second;
                missingRealisations.insert({newRealisation, thisOutputId});
                missingPaths.insert(newRealisation.outPath);
            }
        }
    } else {
        auto outputPaths = outputsAndOptPaths(drv, store);
        for (auto & [outputName, hopefullyOutputPath] : outputPaths) {
            assert(hopefullyOutputPath.second);
            if (!store.isValidPath(*hopefullyOutputPath.second))
                missingPaths.insert(*hopefullyOutputPath.second);
        }
    }

    if (!missingPaths.empty()) {
        Activity act(*logger, lvlTalkative, actUnknown, fmt("copying outputs from '%s'", storeUri));

        /* The goal holds the outputs' locks, so `addToStore` must not take them. `locksHeld` is
           process-wide and the copy suspends, so keep each path in it only while it is copied. */
        auto * localStore = dynamic_cast<LocalStore *>(&store);

        /* Not `copyPaths`, whose `addMultipleToStore` would read the connection from other threads. */
        for (auto & path : sshStore->topoSortPaths(missingPaths) | std::views::reverse) {
            auto printed = store.printStorePath(path);
            if (localStore)
                localStore->locksHeld.lock()->insert(printed); /* FIXME: ugly */
            Finally releaseLockHeld([&] {
                if (localStore)
                    localStore->locksHeld.lock()->erase(printed);
            });
            copyStorePath(*sshStore, store, path, NoRepair, NoCheckSigs);
        }
    }

    for (auto & realisation : missingRealisations) {
        // Should hold, because if the feature isn't enabled the set
        // of missing realisations should be empty
        experimentalFeatureSettings.require(Xp::CaDerivations);
        store.registerDrvOutput(realisation, NoCheckSigs);
    }

    return RemoteMachineBuilt{storeUri, std::move(result)};
}

} // namespace nix
