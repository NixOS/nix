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
#include "store-api.hh"
#include "derivations.hh"

using namespace nix;
using std::cin;

static void handleAlarm(int sig) {
}

std::string escapeUri(std::string uri)
{
    std::replace(uri.begin(), uri.end(), '/', '_');
    return uri;
}

static string currentLoad;

static AutoCloseFD openSlotLock(const Machine & m, unsigned long long slot)
{
    return openLockFile(fmt("%s/%s-%d", currentLoad, escapeUri(m.storeUri), slot), true);
}

int main (int argc, char * * argv)
{
    return handleExceptions(argv[0], [&]() {
        initNix();

        /* Ensure we don't get any SSH passphrase or host key popups. */
        unsetenv("DISPLAY");
        unsetenv("SSH_ASKPASS");

        if (argc != 6)
            throw UsageError("called without required arguments");

        auto store = openStore();

        auto localSystem = argv[1];
        settings.maxSilentTime = std::stoll(argv[2]);
        settings.buildTimeout = std::stoll(argv[3]);
        verbosity = (Verbosity) std::stoll(argv[4]);
        settings.builders = argv[5];

        /* It would be more appropriate to use $XDG_RUNTIME_DIR, since
           that gets cleared on reboot, but it wouldn't work on OS X. */
        currentLoad = settings.nixStateDir + "/current-load";

        std::shared_ptr<Store> sshStore;
        AutoCloseFD bestSlotLock;

        auto machines = getMachines();
        debug("got %d remote builders", machines.size());

        if (machines.empty()) {
            std::cerr << "# decline-permanently\n";
            return;
        }

        string drvPath;
        string storeUri;
        for (string line; getline(cin, line);) {
            auto tokens = tokenizeString<std::vector<string>>(line);
            auto sz = tokens.size();
            if (sz != 3 && sz != 4)
                throw Error("invalid build hook line '%1%'", line);
            auto amWilling = tokens[0] == "1";
            auto neededSystem = tokens[1];
            drvPath = tokens[2];
            auto requiredFeatures = sz == 3 ?
                std::set<string>{} :
                tokenizeString<std::set<string>>(tokens[3], ",");
            auto canBuildLocally = amWilling && (neededSystem == localSystem);

            /* Error ignored here, will be caught later */
            mkdir(currentLoad.c_str(), 0777);

            while (true) {
                bestSlotLock = -1;
                AutoCloseFD lock = openLockFile(currentLoad + "/main-lock", true);
                lockFile(lock.get(), ltWrite, true);

                bool rightType = false;

                Machine * bestMachine = nullptr;
                unsigned long long bestLoad = 0;
                for (auto & m : machines) {
                    debug("considering building on '%s'", m.storeUri);

                    if (m.enabled && std::find(m.systemTypes.begin(),
                            m.systemTypes.end(),
                            neededSystem) != m.systemTypes.end() &&
                        m.allSupported(requiredFeatures) &&
                        m.mandatoryMet(requiredFeatures)) {
                        rightType = true;
                        AutoCloseFD free;
                        unsigned long long load = 0;
                        for (unsigned long long slot = 0; slot < m.maxJobs; ++slot) {
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
                        std::cerr << "# decline\n";
                    break;
                }

#if __APPLE__
                futimes(bestSlotLock.get(), NULL);
#else
                futimens(bestSlotLock.get(), NULL);
#endif

                lock = -1;

                try {

                    Store::Params storeParams{{"max-connections", "1"}, {"log-fd", "4"}};
                    if (bestMachine->sshKey != "")
                        storeParams["ssh-key"] = bestMachine->sshKey;

                    sshStore = openStore(bestMachine->storeUri, storeParams);
                    sshStore->connect();
                    storeUri = bestMachine->storeUri;

                } catch (std::exception & e) {
                    printError("unable to open SSH connection to '%s': %s; trying other available machines...",
                        bestMachine->storeUri, e.what());
                    bestMachine->enabled = false;
                    continue;
                }

                goto connected;
            }
        }

connected:
        std::cerr << "# accept\n";
        string line;
        if (!getline(cin, line))
            throw Error("hook caller didn't send inputs");

        auto inputs = tokenizeString<PathSet>(line);
        if (!getline(cin, line))
            throw Error("hook caller didn't send outputs");

        auto outputs = tokenizeString<PathSet>(line);

        AutoCloseFD uploadLock = openLockFile(currentLoad + "/" + escapeUri(storeUri) + ".upload-lock", true);

        auto old = signal(SIGALRM, handleAlarm);
        alarm(15 * 60);
        if (!lockFile(uploadLock.get(), ltWrite, true))
            printError("somebody is hogging the upload lock for '%s', continuing...");
        alarm(0);
        signal(SIGALRM, old);
        copyPaths(store, ref<Store>(sshStore), inputs, false, true);
        uploadLock = -1;

        BasicDerivation drv(readDerivation(drvPath));
        drv.inputSrcs = inputs;

        printError("building '%s' on '%s'", drvPath, storeUri);
        sshStore->buildDerivation(drvPath, drv);

        PathSet missing;
        for (auto & path : outputs)
            if (!store->isValidPath(path)) missing.insert(path);

        if (!missing.empty()) {
            setenv("NIX_HELD_LOCKS", concatStringsSep(" ", missing).c_str(), 1); /* FIXME: ugly */
            copyPaths(ref<Store>(sshStore), store, missing, false, true);
        }

        return;
    });
}
