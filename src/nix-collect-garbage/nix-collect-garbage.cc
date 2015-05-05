#include "hash.hh"
#include "shared.hh"
#include "globals.hh"

#include <iostream>

using namespace nix;

std::string gen = "old";
bool dryRun = false;

void runProgramSimple(Path program, const Strings & args)
{
    checkInterrupt();

    /* Fork. */
    Pid pid = startProcess([&]() {
        Strings args_(args);
        args_.push_front(program);
        auto cargs = stringsToCharPtrs(args_);

        execv(program.c_str(), (char * *) &cargs[0]);

        throw SysError(format("executing ‘%1%’") % program);
    });

    pid.wait(true);
}


/* If `-d' was specified, remove all old generations of all profiles.
 * Of course, this makes rollbacks to before this point in time
 * impossible. */

void removeOldGenerations(std::string dir)
{
    for (auto & i : readDirectory(dir)) {
        checkInterrupt();

        auto path = dir + "/" + i.name; 
        auto type = getFileType(path);

        if (type == DT_LNK) {
            auto link = readLink(path);
            if (link.find("link") != string::npos) {
                printMsg(lvlInfo, format("removing old generations of profile %1%") % path);

                runProgramSimple(settings.nixBinDir + "/nix-env", Strings{"-p", path, "--delete-generations", gen, dryRun ? "--dry-run" : ""});
            }
        } else if (type == DT_DIR) {
            removeOldGenerations(path);
        }
    }
}

int main(int argc, char * * argv)
{
    bool removeOld = false;
    Strings extraArgs;

    return handleExceptions(argv[0], [&]() {
        initNix();

        parseCmdLine(argc, argv, [&](Strings::iterator & arg, const Strings::iterator & end) {
            if (*arg == "--help")
                showManPage("nix-collect-garbage");
            else if (*arg == "--version")
                printVersion("nix-collect-garbage");
            else if (*arg == "--delete-old" || *arg == "-d") removeOld = true;
            else if (*arg == "--delete-older-than") {
                removeOld = true;
                gen = getArg(*arg, arg, end);
            }
            else if (*arg == "--dry-run") dryRun = true;
            else
                extraArgs.push_back(*arg);
            return true;
        });

        auto profilesDir = settings.nixStateDir + "/profiles";
        if (removeOld) removeOldGenerations(profilesDir);

        // Run the actual garbage collector.
        if (!dryRun) runProgramSimple(settings.nixBinDir + "/nix-store", Strings{"--gc"});
    });
}

