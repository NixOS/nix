#include "store-api.hh"
#include "profiles.hh"
#include "shared.hh"
#include "globals.hh"
#include "legacy.hh"

#include <iostream>
#include <cerrno>

using namespace nix;

std::string deleteOlderThan;
bool dryRun = false;


/* If `-d' was specified, remove all old generations of all profiles.
 * Of course, this makes rollbacks to before this point in time
 * impossible. */

void removeOldGenerations(std::string dir)
{
    if (access(dir.c_str(), R_OK) != 0) return;

    bool canWrite = access(dir.c_str(), W_OK) == 0;

    for (auto & i : readDirectory(dir)) {
        checkInterrupt();

        auto path = dir + "/" + i.name;
        auto type = i.type == DT_UNKNOWN ? getFileType(path) : i.type;

        if (type == DT_LNK && canWrite) {
            std::string link;
            try {
                link = readLink(path);
            } catch (SysError & e) {
                if (e.errNo == ENOENT) continue;
            }
            if (link.find("link") != string::npos) {
                printInfo(format("removing old generations of profile %1%") % path);
                if (deleteOlderThan != "")
                    deleteGenerationsOlderThan(path, deleteOlderThan, dryRun);
                else
                    deleteOldGenerations(path, dryRun);
            }
        } else if (type == DT_DIR) {
            removeOldGenerations(path);
        }
    }
}

static int _main(int argc, char * * argv)
{
    {
        bool removeOld = false;

        GCOptions options;

        parseCmdLine(argc, argv, [&](Strings::iterator & arg, const Strings::iterator & end) {
            if (*arg == "--help")
                showManPage("nix-collect-garbage");
            else if (*arg == "--version")
                printVersion("nix-collect-garbage");
            else if (*arg == "--delete-old" || *arg == "-d") removeOld = true;
            else if (*arg == "--delete-older-than") {
                removeOld = true;
                deleteOlderThan = getArg(*arg, arg, end);
            }
            else if (*arg == "--dry-run") dryRun = true;
            else if (*arg == "--max-freed") {
                long long maxFreed = getIntArg<long long>(*arg, arg, end, true);
                options.maxFreed = maxFreed >= 0 ? maxFreed : 0;
            }
            else
                return false;
            return true;
        });

        initPlugins();

        auto profilesDir = settings.nixStateDir + "/profiles";
        if (removeOld) removeOldGenerations(profilesDir);

        // Run the actual garbage collector.
        if (!dryRun) {
            auto store = openStore();
            options.action = GCOptions::gcDeleteDead;
            GCResults results;
            PrintFreed freed(true, results);
            store->collectGarbage(options, results);
        }

        return 0;
    }
}

static RegisterLegacyCommand s1("nix-collect-garbage", _main);
