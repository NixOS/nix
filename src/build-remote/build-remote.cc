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

class Machine {
    const std::set<string> supportedFeatures;
    const std::set<string> mandatoryFeatures;

public:
    const string hostName;
    const std::vector<string> systemTypes;
    const string sshKey;
    const unsigned int maxJobs;
    const unsigned int speedFactor;
    bool enabled;

    bool allSupported(const std::set<string> & features) const {
        return std::all_of(features.begin(), features.end(),
            [&](const string & feature) {
                return supportedFeatures.count(feature) ||
                    mandatoryFeatures.count(feature);
            });
    }

    bool mandatoryMet(const std::set<string> & features) const {
        return std::all_of(mandatoryFeatures.begin(), mandatoryFeatures.end(),
            [&](const string & feature) {
                return features.count(feature);
            });
    }

    Machine(decltype(hostName) hostName,
        decltype(systemTypes) systemTypes,
        decltype(sshKey) sshKey,
        decltype(maxJobs) maxJobs,
        decltype(speedFactor) speedFactor,
        decltype(supportedFeatures) supportedFeatures,
        decltype(mandatoryFeatures) mandatoryFeatures) :
        supportedFeatures(supportedFeatures),
        mandatoryFeatures(mandatoryFeatures),
        hostName(hostName),
        systemTypes(systemTypes),
        sshKey(sshKey),
        maxJobs(maxJobs),
        speedFactor(std::max(1U, speedFactor)),
        enabled(true)
    {};
};;

static std::vector<Machine> readConf()
{
    auto conf = getEnv("NIX_REMOTE_SYSTEMS", SYSCONFDIR "/nix/machines");

    auto machines = std::vector<Machine>{};
    auto lines = std::vector<string>{};
    try {
        lines = tokenizeString<std::vector<string>>(readFile(conf), "\n");
    } catch (const SysError & e) {
        if (e.errNo != ENOENT)
            throw;
    }
    for (auto line : lines) {
        chomp(line);
        line.erase(std::find(line.begin(), line.end(), '#'), line.end());
        if (line.empty()) {
            continue;
        }
        auto tokens = tokenizeString<std::vector<string>>(line);
        auto sz = tokens.size();
        if (sz < 4)
            throw FormatError("bad machines.conf file ‘%1%’", conf);
        machines.emplace_back(tokens[0],
            tokenizeString<std::vector<string>>(tokens[1], ","),
            tokens[2],
            stoull(tokens[3]),
            sz >= 5 ? stoull(tokens[4]) : 1LL,
            sz >= 6 ?
            tokenizeString<std::set<string>>(tokens[5], ",") :
            std::set<string>{},
            sz >= 7 ?
            tokenizeString<std::set<string>>(tokens[6], ",") :
            std::set<string>{});
    }
    return machines;
}

static string currentLoad;

static AutoCloseFD openSlotLock(const Machine & m, unsigned long long slot)
{
    std::ostringstream fn_stream(currentLoad, std::ios_base::ate | std::ios_base::out);
    fn_stream << "/";
    for (auto t : m.systemTypes) {
        fn_stream << t << "-";
    }
    fn_stream << m.hostName << "-" << slot;
    return openLockFile(fn_stream.str(), true);
}

static char display_env[] = "DISPLAY=";
static char ssh_env[] = "SSH_ASKPASS=";

int main (int argc, char * * argv)
{
    return handleExceptions(argv[0], [&]() {
        initNix();

        /* Ensure we don't get any SSH passphrase or host key popups. */
        if (putenv(display_env) == -1 ||
            putenv(ssh_env) == -1)
            throw SysError("setting SSH env vars");

        if (argc != 4)
            throw UsageError("called without required arguments");

        auto store = openStore();

        auto localSystem = argv[1];
        settings.maxSilentTime = stoull(string(argv[2]));
        settings.buildTimeout = stoull(string(argv[3]));

        currentLoad = getEnv("NIX_CURRENT_LOAD", "/run/nix/current-load");

        std::shared_ptr<Store> sshStore;
        AutoCloseFD bestSlotLock;

        auto machines = readConf();
        string drvPath;
        string hostName;
        for (string line; getline(cin, line);) {
            auto tokens = tokenizeString<std::vector<string>>(line);
            auto sz = tokens.size();
            if (sz != 3 && sz != 4)
                throw Error("invalid build hook line ‘%1%’", line);
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
                    sshStore = openStore("ssh-ng://" + bestMachine->hostName,
                        { {"ssh-key", bestMachine->sshKey },
                          {"max-connections", "1" } });
                    hostName = bestMachine->hostName;
                } catch (std::exception & e) {
                    printError("unable to open SSH connection to ‘%s’: %s; trying other available machines...",
                        bestMachine->hostName, e.what());
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
        AutoCloseFD uploadLock = openLockFile(currentLoad + "/" + hostName + ".upload-lock", true);
        auto old = signal(SIGALRM, handleAlarm);
        alarm(15 * 60);
        if (!lockFile(uploadLock.get(), ltWrite, true))
            printError("somebody is hogging the upload lock for ‘%s’, continuing...");
        alarm(0);
        signal(SIGALRM, old);
        copyPaths(store, ref<Store>(sshStore), inputs);
        uploadLock = -1;

        printError("building ‘%s’ on ‘%s’", drvPath, hostName);
        sshStore->buildDerivation(drvPath, readDerivation(drvPath));

        PathSet missing;
        for (auto & path : outputs)
            if (!store->isValidPath(path)) missing.insert(path);

        if (!missing.empty()) {
            setenv("NIX_HELD_LOCKS", concatStringsSep(" ", missing).c_str(), 1); /* FIXME: ugly */
            copyPaths(ref<Store>(sshStore), store, missing);
        }

        return;
    });
}
