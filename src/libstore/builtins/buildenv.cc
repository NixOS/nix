#include "buildenv.hh"
#include "derivations.hh"
#include "file-system.hh"
#include "signals.hh"

#include <filesystem>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <algorithm>

namespace nix {

struct State
{
    std::map<Path, int> priorities;
    unsigned long symlinks = 0;
};

/* For each activated package, create symlinks */
static void createLinks(State & state, const Path & srcDir, const Path & dstDir, int priority)
{
    namespace fs = std::filesystem;

    fs::directory_iterator srcFiles;

    try {
        srcFiles = fs::directory_iterator{srcDir};
    } catch (fs::filesystem_error & e) {
        if (e.code() == std::errc::not_a_directory) {
            warn("not including '%s' in the user environment because it's not a directory", srcDir);
            return;
        }
        throw;
    }

    for (const auto & ent : srcFiles) {
        checkInterrupt();
        auto name = ent.path().filename();
        if (name.string()[0] == '.')
            /* not matched by glob */
            continue;
        auto srcFile = (fs::path{srcDir} / name).string();
        auto dstFile = (fs::path{dstDir} / name).string();

        fs::file_status srcSt;

        try {
            srcSt = fs::status(srcFile);

            if (srcSt.type() == fs::file_type::not_found) {
                warn("skipping dangling symlink '%s'", dstFile);
                continue;
            }
        } catch (fs::filesystem_error & e) {
            throw SysError("getting status of '%1%'", srcFile);
        }

        /* The files below are special-cased to that they don't show
         * up in user profiles, either because they are useless, or
         * because they would cause pointless collisions (e.g., each
         * Python package brings its own
         * `$out/lib/pythonX.Y/site-packages/easy-install.pth'.)
         */
        if (hasSuffix(srcFile, "/propagated-build-inputs") ||
            hasSuffix(srcFile, "/nix-support") ||
            hasSuffix(srcFile, "/perllocal.pod") ||
            hasSuffix(srcFile, "/info/dir") ||
            hasSuffix(srcFile, "/log") ||
            hasSuffix(srcFile, "/manifest.nix") ||
            hasSuffix(srcFile, "/manifest.json"))
            continue;

        else if (fs::is_directory(srcSt)) {
            auto dstStOpt = maybeSymlinkStat(dstFile);
            if (dstStOpt) {
                auto & dstSt = *dstStOpt;
                if (fs::is_directory(dstSt)) {
                    createLinks(state, srcFile, dstFile, priority);
                    continue;
                } else if (fs::is_symlink(dstSt)) {
                    auto target = canonPath(dstFile, true);
                    if (!fs::is_directory(fs::symlink_status(target)))
                        throw Error("collision between '%1%' and non-directory '%2%'", srcFile, target);
                    if (unlink(dstFile.c_str()) == -1)
                        throw SysError("unlinking '%1%'", dstFile);
                    if (mkdir(dstFile.c_str()
                #ifndef _WIN32 // TODO abstract mkdir perms for Windows
                            , 0755
                #endif
                            ) == -1)
                        throw SysError("creating directory '%1%'", dstFile);
                    createLinks(state, target, dstFile, state.priorities[dstFile]);
                    createLinks(state, srcFile, dstFile, priority);
                    continue;
                }
            }
        }

        else {
            auto dstStOpt = maybeSymlinkStat(dstFile);
            if (dstStOpt) {
                auto & dstSt = *dstStOpt;
                if (fs::is_symlink(dstSt)) {
                    auto prevPriority = state.priorities[dstFile];
                    if (prevPriority == priority)
                        throw BuildEnvFileConflictError(
                            readLink(dstFile),
                            srcFile,
                            priority
                        );
                    if (prevPriority < priority)
                        continue;
                    if (unlink(dstFile.c_str()) == -1)
                        throw SysError("unlinking '%1%'", dstFile);
                } else if (fs::is_directory(dstSt))
                    throw Error("collision between non-directory '%1%' and directory '%2%'", srcFile, dstFile);
            }
        }

        createSymlink(srcFile, dstFile);
        state.priorities[dstFile] = priority;
        state.symlinks++;
    }
}

void buildProfile(const Path & out, Packages && pkgs)
{
    State state;

    std::set<Path> done, postponed;

    auto addPkg = [&](const Path & pkgDir, int priority) {
        if (!done.insert(pkgDir).second) return;
        createLinks(state, pkgDir, out, priority);

        try {
            for (const auto & p : tokenizeString<std::vector<std::string>>(
                    readFile(pkgDir + "/nix-support/propagated-user-env-packages"), " \n"))
                if (!done.count(p))
                    postponed.insert(p);
        } catch (SysError & e) {
            if (e.errNo != ENOENT && e.errNo != ENOTDIR) throw;
        }
    };

    /* Symlink to the packages that have been installed explicitly by the
     * user. Process in priority order to reduce unnecessary
     * symlink/unlink steps.
     */
    std::sort(pkgs.begin(), pkgs.end(), [](const Package & a, const Package & b) {
        return a.priority < b.priority || (a.priority == b.priority && a.path < b.path);
    });
    for (const auto & pkg : pkgs)
        if (pkg.active)
            addPkg(pkg.path, pkg.priority);

    /* Symlink to the packages that have been "propagated" by packages
     * installed by the user (i.e., package X declares that it wants Y
     * installed as well). We do these later because they have a lower
     * priority in case of collisions.
     */
    auto priorityCounter = 1000;
    while (!postponed.empty()) {
        std::set<Path> pkgDirs;
        postponed.swap(pkgDirs);
        for (const auto & pkgDir : pkgDirs)
            addPkg(pkgDir, priorityCounter++);
    }

    debug("created %d symlinks in user environment", state.symlinks);
}

void builtinBuildenv(
    const BasicDerivation & drv,
    const std::map<std::string, Path> & outputs)
{
    auto getAttr = [&](const std::string & name) {
        auto i = drv.env.find(name);
        if (i == drv.env.end()) throw Error("attribute '%s' missing", name);
        return i->second;
    };

    auto out = outputs.at("out");
    createDirs(out);

    /* Convert the stuff we get from the environment back into a
     * coherent data type. */
    Packages pkgs;
    {
        auto derivations = tokenizeString<Strings>(getAttr("derivations"));

        auto itemIt = derivations.begin();
        while (itemIt != derivations.end()) {
            /* !!! We're trusting the caller to structure derivations env var correctly */
            const bool active = "false" != *itemIt++;
            const int priority = stoi(*itemIt++);
            const size_t outputs = stoul(*itemIt++);

            for (size_t n {0}; n < outputs; n++) {
                pkgs.emplace_back(std::move(*itemIt++), active, priority);
            }
        }
    }

    buildProfile(out, std::move(pkgs));

    createSymlink(getAttr("manifest"), out + "/manifest.nix");
}

}
