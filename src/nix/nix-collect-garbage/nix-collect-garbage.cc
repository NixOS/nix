#include "nix/util/file-system.hh"
#include "nix/util/signals.hh"
#include "nix/util/error.hh"
#include "nix/store/store-open.hh"
#include "nix/store/store-cast.hh"
#include "nix/store/gc-store.hh"
#include "nix/store/profiles.hh"
#include "nix/main/shared.hh"
#include "nix/store/globals.hh"
#include "nix/cmd/legacy.hh"
#include "man-pages.hh"

#include <iostream>
#include <cerrno>

using namespace nix;

std::string deleteOlderThan;
bool dryRun = false;

/* If `-d' was specified, remove all old generations of all profiles.
 * Of course, this makes rollbacks to before this point in time
 * impossible. */

void removeOldGenerations(std::filesystem::path dir)
{
    if (access(dir.string().c_str(), R_OK) != 0)
        return;

    bool canWrite = access(dir.string().c_str(), W_OK) == 0;

    for (auto & i : DirectoryIterator{dir}) {
        checkInterrupt();

        auto path = i.path().string();
        auto type = i.symlink_status().type();

        if (type == std::filesystem::file_type::symlink && canWrite) {
            std::string link;
            try {
                link = readLink(path);
            } catch (SystemError & e) {
                if (e.is(std::errc::no_such_file_or_directory))
                    continue;
                throw;
            }
            if (link.find("link") != std::string::npos) {
                printInfo("removing old generations of profile %s", path);
                if (deleteOlderThan != "") {
                    auto t = parseOlderThanTimeSpec(deleteOlderThan);
                    deleteGenerationsOlderThan(path, t, dryRun);
                } else
                    deleteOldGenerations(path, dryRun);
            }
        } else if (type == std::filesystem::file_type::directory) {
            removeOldGenerations(path);
        }
    }
}

static int main_nix_collect_garbage(int argc, char ** argv)
{
    {
        bool removeOld = false;

        GCOptions options;

        parseCmdLine(argc, argv, [&](Strings::iterator & arg, const Strings::iterator & end) {
            if (*arg == "--help")
                showManPage("nix-collect-garbage");
            else if (*arg == "--version")
                printVersion("nix-collect-garbage");
            else if (*arg == "--delete-old" || *arg == "-d")
                removeOld = true;
            else if (*arg == "--delete-older-than") {
                removeOld = true;
                deleteOlderThan = getArg(*arg, arg, end);
            } else if (*arg == "--dry-run")
                dryRun = true;
            else if (*arg == "--max-freed")
                options.maxFreed = std::max(getIntArg<int64_t>(*arg, arg, end, true), (int64_t) 0);
            else
                return false;
            return true;
        });

        if (options.maxFreed != std::numeric_limits<uint64_t>::max() && dryRun)
            throw UsageError("options --max-freed and --dry-run cannot be combined");

        if (removeOld) {
            auto profilesDirOpts = settings.getProfileDirsOptions();
            std::set<std::filesystem::path> dirsToClean = {
                profilesDir(profilesDirOpts),
                std::filesystem::path{settings.nixStateDir} / "profiles",
                getDefaultProfile(profilesDirOpts).parent_path(),
            };
            for (auto & dir : dirsToClean)
                removeOldGenerations(dir);
        }

        auto store = openStore();
        auto & gcStore = require<GcStore>(*store);
        options.action = dryRun ? GCOptions::gcReturnDead : GCOptions::gcDeleteDead;
        GCResults results;
        Finally printer([&] { printFreed(dryRun, results); });
        gcStore.collectGarbage(options, results);

        return 0;
    }
}

static RegisterLegacyCommand r_nix_collect_garbage("nix-collect-garbage", main_nix_collect_garbage);
