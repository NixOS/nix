/*
 * A very simple utility to trace all the gc roots through the file-system
 * The reason for this program is that tracing these roots is the only part of
 * Nix that requires to run as root (because it requires reading through the
 * user home directories to resolve the indirect roots)
 *
 * This program intentionnally doesnt depend on any Nix library to reduce the attack surface.
 */

#include <filesystem>
#include <iostream>
#include <set>
#include <vector>
#include <algorithm>
#include <getopt.h>

namespace fs = std::filesystem;
using std::set, std::string;

struct GlobalOpts {
    fs::path storeDir = "/nix/store";
    fs::path stateDir = "/nix/var/nix";
    enum VerbosityLvl {
        Quiet,
        Verbose
    };
    VerbosityLvl verbosity = Quiet;
};

void log(GlobalOpts::VerbosityLvl verbosity, std::string_view msg)
{
    if (verbosity == GlobalOpts::Quiet)
        return;
    std::cerr << msg << std::endl;
}

GlobalOpts parseCmdLine(int argc, char** argv)
{
    GlobalOpts res;
    auto usage = [&]() {
        std::cerr << "Usage: " << string(argv[0]) << " [--verbose|-v] [-s storeDir] [-d stateDir]" << std::endl;
        exit(1);
    };
    static struct option long_options[] = {
        { "verbose", no_argument, (int*)&res.verbosity, GlobalOpts::Verbose },
        { "store_dir", required_argument, 0, 's' },
        { "state_dir", required_argument, 0, 'd' },
    };

    int option_index = 0;
    int opt_char;
    while((opt_char = getopt_long(argc, argv, "vs:",
                    long_options, &option_index)) != -1) {
        switch (opt_char) {
            case 0:
                break;
          break;
            case '?':
                usage();
                break;
            case 'v':
                res.verbosity = GlobalOpts::Verbose;
                break;
            case 's':
                res.storeDir = fs::path(optarg);
                break;
            case 'd':
                res.stateDir = fs::path(optarg);
                break;
            default:
                std::cerr << "Got invalid char: " << (char)opt_char << std::endl;
                abort();
        }
    };
    return res;
}

/*
 * A value of type `Roots` is a mapping from a store path to the set of roots that keep it alive
 */
typedef std::map<fs::path, std::set<fs::path>> Roots;
struct TraceResult {
    Roots storeRoots;
    set<fs::path> deadLinks;
};

bool isInStore(fs::path storeDir, fs::path dir)
{
    return (std::search(dir.begin(), dir.end(), storeDir.begin(), storeDir.end()) == dir.begin());
}

void followPathToStore(
    const GlobalOpts & opts,
    int recursionsLeft,
    TraceResult & res,
    const fs::path & root,
    const fs::file_status & status)
{
    log(opts.verbosity, "Considering file " + root.string());

    if (recursionsLeft < 0)
        return;

    switch (status.type()) {
        case fs::file_type::directory:
            {
                auto directory_iterator = fs::recursive_directory_iterator(root);
                for (auto & child : directory_iterator)
                    followPathToStore(opts, recursionsLeft, res, child.path(), child.symlink_status());
                break;
            }
        case fs::file_type::symlink:
            {
                auto target = root.parent_path() / fs::read_symlink(root);
                auto not_found = [&](std::string msg) {
                    log(opts.verbosity, "Error accessing the file " + target.string() + ": " + msg);
                    log(opts.verbosity, "(When resolving the symlink " + root.string() + ")");
                    res.deadLinks.insert(root);
                };
                try {
                    auto target_status = fs::symlink_status(target);
                    if (target_status.type() == fs::file_type::not_found)
                        not_found("Not found");

                    if (isInStore(opts.storeDir, target)) {
                        res.storeRoots[target].insert(root);
                        return;
                    } else {
                        followPathToStore(opts, recursionsLeft - 1, res, target, target_status);
                    }

                } catch (fs::filesystem_error & e) {
                    not_found(e.what());
                }
            }
        case fs::file_type::regular:
            {
                auto possibleStorePath = opts.storeDir / root.filename();
                if (fs::exists(possibleStorePath))
                res.storeRoots[possibleStorePath].insert(root);
            }
        default:
            break;
    }
}

void followPathToStore(
    const GlobalOpts & opts,
    int recursionsLeft,
    TraceResult & res,
    const fs::path & root)
{
    try {
        auto status = fs::symlink_status(root);
        followPathToStore(opts, recursionsLeft, res, root, status);
    } catch (fs::filesystem_error & e) {
        log(opts.verbosity, "Error accessing the file " + root.string() + ": " + e.what());
    }
}

/*
 * Return the set of all the store paths that are reachable from the given set
 * of filesystem paths, by:
 * - descending into the directories
 * - following the symbolic links (at most twice)
 * - reading the name of regular files (when encountering a file
 *   `/foo/bar/abcdef`, the algorithm will try to access `/nix/store/abcdef`)
 *
 * Also returns the set of all dead links encountered during the process (so
 * that they can be removed if it makes sense).
 */
TraceResult followPathsToStore(GlobalOpts opts, set<fs::path> roots)
{
    int maxRecursionLevel = 2;
    TraceResult res;
    for (auto & root : roots)
        followPathToStore(opts, maxRecursionLevel, res, root);
    return res;
}

int main(int argc, char * * argv)
{
    GlobalOpts opts = parseCmdLine(argc, argv);
    set<fs::path> standardRoots = {
        opts.stateDir / fs::path("profiles"),
        opts.stateDir / fs::path("gcroots"),
    };
    auto traceResult = followPathsToStore(opts, standardRoots);
    for (auto & [rootInStore, externalRoots] : traceResult.storeRoots) {
        for (auto & externalRoot : externalRoots)
            std::cout << rootInStore.string() << '\t' << externalRoot.string() << std::endl;
    }
    std::cout << std::endl;
    for (auto & deadLink : traceResult.deadLinks) {
        std::cout << deadLink.string() << std::endl;
    }
}

