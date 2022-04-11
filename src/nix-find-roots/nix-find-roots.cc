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
#include <regex>
#include <set>
#include <vector>
#include <algorithm>
#include <getopt.h>
#include <fstream>

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

string quoteRegexChars(const string & raw)
{
    static auto specialRegex = std::regex(R"([.^$\\*+?()\[\]{}|])");
    return std::regex_replace(raw, specialRegex, R"(\$&)");
}
std::regex storePathRegex(const fs::path storeDir)
{
    return std::regex(quoteRegexChars(storeDir) + R"(/[0-9a-z]+[0-9a-zA-Z\+\-\._\?=]*)");
}

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

/**
 * Scan the content of the given file for al the occurences of something that looks
 * like a store path (i.e. that matches `storePathRegex(opts.storeDir)`) and add them
 * to `res`
 */
void scanFileContent(const GlobalOpts & opts, const fs::path & fileToScan, Roots & res)
{
    if (!fs::exists(fileToScan))
        return;

    std::ostringstream contentStream;
    {
        std::ifstream fs;
        fs.open(fileToScan);
        fs >> contentStream.rdbuf();
    }
    std::string content = contentStream.str();
    auto regex = storePathRegex(opts.storeDir);
    auto firstMatch
        = std::sregex_iterator { content.begin(), content.end(), regex };
    auto fileEnd = std::sregex_iterator{};
    for (auto i = firstMatch; i != fileEnd; ++i)
        res[i->str()].emplace(fileToScan);
}

/**
 * Scan the content of a `/proc/[pid]/maps` file for regions that are mmaped to
 * a store path
 */
void scanMapsFile(const GlobalOpts & opts, const fs::path & mapsFile, Roots & res)
{
    if (!fs::exists(mapsFile))
        return;

    static auto mapRegex = std::regex(R"(^\s*\S+\s+\S+\s+\S+\s+\S+\s+\S+\s+(/\S+)\s*$)");
    std::stringstream mappedFile;
    {
        std::ifstream fs;
        fs.open(mapsFile);
        fs >> mappedFile.rdbuf();
    }
    std::string line;
    while (std::getline(mappedFile, line)) {
        auto match = std::smatch{};
        if (std::regex_match(line, match, mapRegex)) {
            auto matchedPath = fs::path(match[1]);
            if (isInStore(opts.storeDir, matchedPath))
                res[fs::path(match[1])].emplace(mapsFile);
        }
    }

}

Roots getRuntimeRoots(GlobalOpts opts)
{
    auto procDir = fs::path("/proc");
    if (!fs::exists(procDir))
        return {};
    Roots res;
    auto digitsRegex = std::regex(R"(^\d+$)");
    for (auto & procEntry : fs::directory_iterator(procDir)) {
        // Only the directories whose name is a sequence of digits represent
        // pids
        if (!std::regex_match(procEntry.path().filename().string(), digitsRegex)
            || !procEntry.is_directory())
            continue;

        log(opts.verbosity, "Considering path " + procEntry.path().string());

        // A set of paths used by the executable and possibly symlinks to a
        // path in the store
        set<fs::path> pathsToConsider;
        pathsToConsider.insert(procEntry.path()/"exe");
        pathsToConsider.insert(procEntry.path()/"cwd");
        try {
            auto fdDir = procEntry.path()/"fd";
            for (auto & fdFile : fs::directory_iterator(fdDir))
                pathsToConsider.insert(fdFile.path());
        } catch (fs::filesystem_error & e) {
            if (e.code().value() != ENOENT && e.code().value() != EACCES)
                throw;
        }
        for (auto & path : pathsToConsider) try {
            auto realPath = fs::read_symlink(path);
            if (isInStore(opts.storeDir, realPath))
                res[realPath].insert(path);
        } catch (fs::filesystem_error &e) {
            log(opts.verbosity, e.what());
        }

        // Scan the environment of the executable
        scanFileContent(opts, procEntry.path()/"environ", res);
        scanMapsFile(opts, procEntry.path()/"maps", res);
    }

    // Mostly useful for NixOS, but doesn’t hurt to check on other systems
    // anyways
    scanFileContent(opts, "/proc/sys/kernel/modprobe", res);
    scanFileContent(opts, "/proc/sys/kernel/fbsplash", res);
    scanFileContent(opts, "/proc/sys/kernel/poweroff_cmd", res);

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
    auto runtimeRoots = getRuntimeRoots(opts);
    traceResult.storeRoots.insert(runtimeRoots.begin(), runtimeRoots.end());
    for (auto & [rootInStore, externalRoots] : traceResult.storeRoots) {
        for (auto & externalRoot : externalRoots)
            std::cout << rootInStore.string() << '\t' << externalRoot.string() << std::endl;
    }
    std::cout << std::endl;
    for (auto & deadLink : traceResult.deadLinks) {
        std::cout << deadLink.string() << std::endl;
    }
}

