#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <set>
#include <memory>
#include <tuple>
#include <iomanip>
#if __APPLE__
#include <sys/time.h>
#endif

#include "machines.hh"
#include "shared.hh"
#include "pathlocks.hh"
#include "globals.hh"
#include "serialise.hh"
#include "build-result.hh"
#include "store-api.hh"
#include "derivations.hh"
#include "local-store.hh"
#include "legacy.hh"
#include "experimental-features.hh"

using namespace nix;
using std::cin;

static void handleAlarm(int sig) {
}

std::string escapeUri(std::string uri)
{
    std::replace(uri.begin(), uri.end(), '/', '_');
    return uri;
}

static std::string currentLoad;

static AutoCloseFD openSlotLock(const Machine & m, uint64_t slot)
{
    return openLockFile(fmt("%s/%s-%d", currentLoad, escapeUri(m.storeUri), slot), true);
}

static bool allSupportedLocally(Store & store, const std::set<std::string>& requiredFeatures) {
    for (auto & feature : requiredFeatures)
        if (!store.systemFeatures.get().count(feature)) return false;
    return true;
}

static int main_build_remote(int argc, char * * argv)
{
    {
        logger = makeJSONLogger(*logger);

        /* Ensure we don't get any SSH passphrase or host key popups. */
        unsetenv("DISPLAY");
        unsetenv("SSH_ASKPASS");

        /* If we ever use the common args framework, make sure to
           remove initPlugins below and initialize settings first.
        */
        if (argc != 2)
            throw UsageError("called without required arguments");

        verbosity = (Verbosity) std::stoll(argv[1]);

        FdSource source(STDIN_FILENO);

        /* Read the parent's settings. */
        while (readInt(source)) {
            auto name = readString(source);
            auto value = readString(source);
            settings.set(name, value);
        }

        auto maxBuildJobs = settings.maxBuildJobs;
        settings.maxBuildJobs.set("1"); // hack to make tests with local?root= work

        initPlugins();

        auto store = openStore();

        /* It would be more appropriate to use $XDG_RUNTIME_DIR, since
           that gets cleared on reboot, but it wouldn't work on macOS. */
        auto currentLoadName = "/current-load";
        if (auto localStore = store.dynamic_pointer_cast<LocalFSStore>())
            currentLoad = std::string { localStore->stateDir } + currentLoadName;
        else
            currentLoad = settings.nixStateDir + currentLoadName;

        std::shared_ptr<Store> sshStore;
        AutoCloseFD bestSlotLock;

        auto machines = getMachines();
        debug("got %d remote builders", machines.size());

        if (machines.empty()) {
            std::cerr << "# decline-permanently\n";
            return 0;
        }

        std::optional<StorePath> drvPath;
        std::string storeUri;

        while (true) {

            try {
                auto s = readString(source);
                if (s != "try") return 0;
            } catch (EndOfFile &) { return 0; }

            auto amWilling = readInt(source);
            auto neededSystem = readString(source);
            drvPath = store->parseStorePath(readString(source));
            auto requiredFeatures = readStrings<std::set<std::string>>(source);

            /* It would be possible to build locally after some builds clear out,
               so don't show the warning now: */
            bool couldBuildLocally = maxBuildJobs > 0
                 &&  (  neededSystem == settings.thisSystem
                     || settings.extraPlatforms.get().count(neededSystem) > 0)
                 &&  allSupportedLocally(*store, requiredFeatures);
            /* It's possible to build this locally right now: */
            bool canBuildLocally = amWilling && couldBuildLocally;

            /* Error ignored here, will be caught later */
            mkdir(currentLoad.c_str(), 0777);

            while (true) {
                bestSlotLock = -1;
                AutoCloseFD lock = openLockFile(currentLoad + "/main-lock", true);
                lockFile(lock.get(), ltWrite, true);

                bool rightType = false;

                Machine * bestMachine = nullptr;
                uint64_t bestLoad = 0;
                for (auto & m : machines) {
                    debug("considering building on remote machine '%s'", m.storeUri);

                    if (m.enabled
                        && (neededSystem == "builtin"
                            || std::find(m.systemTypes.begin(),
                                m.systemTypes.end(),
                                neededSystem) != m.systemTypes.end()) &&
                        m.allSupported(requiredFeatures) &&
                        m.mandatoryMet(requiredFeatures))
                    {
                        rightType = true;
                        AutoCloseFD free;
                        uint64_t load = 0;
                        for (uint64_t slot = 0; slot < m.maxJobs; ++slot) {
                            auto slotLock = openSlotLock(m, slot);
                            if (lockFile(slotLock.get(), ltWrite, false)) {
                                if (!free) {
                                    free = std::move(slotLock);
                                }
                            } else {
                                ++load;
                            }
                        }
                        if (!free) {
                            continue;
                        }
                        bool best = false;
                        if (!bestSlotLock) {
                            best = true;
                        } else if (load / m.speedFactor < bestLoad / bestMachine->speedFactor) {
                            best = true;
                        } else if (load / m.speedFactor == bestLoad / bestMachine->speedFactor) {
                            if (m.speedFactor > bestMachine->speedFactor) {
                                best = true;
                            } else if (m.speedFactor == bestMachine->speedFactor) {
                                if (load < bestLoad) {
                                    best = true;
                                }
                            }
                        }
                        if (best) {
                            bestLoad = load;
                            bestSlotLock = std::move(free);
                            bestMachine = &m;
                        }
                    }
                }

                if (!bestSlotLock) {
                    if (rightType && !canBuildLocally)
                        std::cerr << "# postpone\n";
                    else
                    {
                        // build the hint template.
                        std::string errorText =
                            "Failed to find a machine for remote build!\n"
                            "derivation: %s\nrequired (system, features): (%s, [%s])";
                        errorText += "\n%s available machines:";
                        errorText += "\n(systems, maxjobs, supportedFeatures, mandatoryFeatures)";

                        for (unsigned int i = 0; i < machines.size(); ++i)
                            errorText += "\n([%s], %s, [%s], [%s])";

                        // add the template values.
                        std::string drvstr;
                        if (drvPath.has_value())
                            drvstr = drvPath->to_string();
                        else
                            drvstr = "<unknown>";

                        auto error = hintformat(errorText);
                        error
                            % drvstr
                            % neededSystem
                            % concatStringsSep<StringSet>(", ", requiredFeatures)
                            % machines.size();

                        for (auto & m : machines)
                            error
                                % concatStringsSep<std::vector<std::string>>(", ", m.systemTypes)
                                % m.maxJobs
                                % concatStringsSep<StringSet>(", ", m.supportedFeatures)
                                % concatStringsSep<StringSet>(", ", m.mandatoryFeatures);

                        printMsg(couldBuildLocally ? lvlChatty : lvlWarn, error.str());

                        std::cerr << "# decline\n";
                    }
                    break;
                }

#if __APPLE__
                futimes(bestSlotLock.get(), NULL);
#else
                futimens(bestSlotLock.get(), NULL);
#endif

                lock = -1;

                try {

                    Activity act(*logger, lvlTalkative, actUnknown, fmt("connecting to '%s'", bestMachine->storeUri));

                    sshStore = bestMachine->openStore();
                    sshStore->connect();
                    storeUri = bestMachine->storeUri;

                } catch (std::exception & e) {
                    auto msg = chomp(drainFD(5, false));
                    printError("cannot build on '%s': %s%s",
                        bestMachine->storeUri, e.what(),
                        msg.empty() ? "" : ": " + msg);
                    bestMachine->enabled = false;
                    continue;
                }

                goto connected;
            }
        }

connected:
        close(5);

        assert(sshStore);

        std::cerr << "# accept\n" << storeUri << "\n";

        auto inputs = readStrings<PathSet>(source);
        auto wantedOutputs = readStrings<StringSet>(source);

        AutoCloseFD uploadLock = openLockFile(currentLoad + "/" + escapeUri(storeUri) + ".upload-lock", true);

        {
            Activity act(*logger, lvlTalkative, actUnknown, fmt("waiting for the upload lock to '%s'", storeUri));

            auto old = signal(SIGALRM, handleAlarm);
            alarm(15 * 60);
            if (!lockFile(uploadLock.get(), ltWrite, true))
                printError("somebody is hogging the upload lock for '%s', continuing...");
            alarm(0);
            signal(SIGALRM, old);
        }

        auto substitute = settings.buildersUseSubstitutes ? Substitute : NoSubstitute;

        {
            Activity act(*logger, lvlTalkative, actUnknown, fmt("copying dependencies to '%s'", storeUri));
            copyPaths(*store, *sshStore, store->parseStorePathSet(inputs), NoRepair, NoCheckSigs, substitute);
        }

        uploadLock = -1;

        auto drv = store->readDerivation(*drvPath);

        std::optional<BuildResult> optResult;

        // If we don't know whether we are trusted (e.g. `ssh://`
        // stores), we assume we are. This is necessary for backwards
        // compat.
        bool trustedOrLegacy = ({
            std::optional trusted = sshStore->isTrustedClient();
            !trusted || *trusted;
        });

        // See the very large comment in `case WorkerProto::Op::BuildDerivation:` in
        // `src/libstore/daemon.cc` that explains the trust model here.
        //
        // This condition mirrors that: that code enforces the "rules" outlined there;
        // we do the best we can given those "rules".
        if (trustedOrLegacy || drv.type().isCA())  {
            // Hijack the inputs paths of the derivation to include all
            // the paths that come from the `inputDrvs` set. We don’t do
            // that for the derivations whose `inputDrvs` is empty
            // because:
            //
            // 1. It’s not needed
            //
            // 2. Changing the `inputSrcs` set changes the associated
            //    output ids, which break CA derivations
            if (!drv.inputDrvs.empty())
                drv.inputSrcs = store->parseStorePathSet(inputs);
            optResult = sshStore->buildDerivation(*drvPath, (const BasicDerivation &) drv);
            auto & result = *optResult;
            if (!result.success())
                throw Error("build of '%s' on '%s' failed: %s", store->printStorePath(*drvPath), storeUri, result.errorMsg);
        } else {
            copyClosure(*store, *sshStore, StorePathSet {*drvPath}, NoRepair, NoCheckSigs, substitute);
            auto res = sshStore->buildPathsWithResults({
                DerivedPath::Built {
                    .drvPath = makeConstantStorePathRef(*drvPath),
                    .outputs = OutputsSpec::All {},
                }
            });
            // One path to build should produce exactly one build result
            assert(res.size() == 1);
            optResult = std::move(res[0]);
        }


        auto outputHashes = staticOutputHashes(*store, drv);
        std::set<Realisation> missingRealisations;
        StorePathSet missingPaths;
        if (experimentalFeatureSettings.isEnabled(Xp::CaDerivations) && !drv.type().hasKnownOutputPaths()) {
            for (auto & outputName : wantedOutputs) {
                auto thisOutputHash = outputHashes.at(outputName);
                auto thisOutputId = DrvOutput{ thisOutputHash, outputName };
                if (!store->queryRealisation(thisOutputId)) {
                    debug("missing output %s", outputName);
                    assert(optResult);
                    auto & result = *optResult;
                    auto i = result.builtOutputs.find(outputName);
                    assert(i != result.builtOutputs.end());
                    auto & newRealisation = i->second;
                    missingRealisations.insert(newRealisation);
                    missingPaths.insert(newRealisation.outPath);
                }
            }
        } else {
            auto outputPaths = drv.outputsAndOptPaths(*store);
            for (auto & [outputName, hopefullyOutputPath] : outputPaths) {
                assert(hopefullyOutputPath.second);
                if (!store->isValidPath(*hopefullyOutputPath.second))
                    missingPaths.insert(*hopefullyOutputPath.second);
            }
        }

        if (!missingPaths.empty()) {
            Activity act(*logger, lvlTalkative, actUnknown, fmt("copying outputs from '%s'", storeUri));
            if (auto localStore = store.dynamic_pointer_cast<LocalStore>())
                for (auto & path : missingPaths)
                    localStore->locksHeld.insert(store->printStorePath(path)); /* FIXME: ugly */
            copyPaths(*sshStore, *store, missingPaths, NoRepair, NoCheckSigs, NoSubstitute);
        }
        // XXX: Should be done as part of `copyPaths`
        for (auto & realisation : missingRealisations) {
            // Should hold, because if the feature isn't enabled the set
            // of missing realisations should be empty
            experimentalFeatureSettings.require(Xp::CaDerivations);
            store->registerDrvOutput(realisation);
        }

        return 0;
    }
}

static RegisterLegacyCommand r_build_remote("build-remote", main_build_remote);
