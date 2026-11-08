#include "nix/store/builtins/buildenv.hh"
#include "nix/store/builtins.hh"
#include "nix/store/derivations.hh"
#include "nix/util/signals.hh"
#include "nix/util/system-error.hh"
#include "nix/util/file-system-at.hh"

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <algorithm>

namespace nix {

void BuildEnvFileConflictError::anchor() {}

RegisterBuiltinBuilder::BuiltinBuilders & RegisterBuiltinBuilder::builtinBuilders()
{
    static RegisterBuiltinBuilder::BuiltinBuilders builders;
    return builders;
}

namespace {

struct State
{
    std::map<std::filesystem::path, int> priorities;
    unsigned long symlinks = 0;
};

} // namespace

/**
 * Create the directory @p name in @p dirFd, which is at @p path, and
 * open it.
 */
static AutoCloseFD createDirectory(Descriptor dirFd, const OsFilename & name, const std::filesystem::path & path)
{
    auto fdOrError = createDirectoryAt(
        dirFd,
        name
#ifndef _WIN32 // TODO abstract mkdir perms for Windows
        ,
        0755
#endif
    );
    if (!fdOrError)
        throw NativeSysError(fdOrError.error(), "creating directory %s", PathFmt(path));
    auto & fd = fdOrError.value();
    if (fd)
        return std::move(fd);
    return valueOr(
        openDirectoryAt(dirFd, name), NIX_THROW_NATIVE_SYS_ERROR_HANDLER("opening directory %s", PathFmt(path)));
}

/* For each activated package, create symlinks in @p dstDirFd, which is
   at @p dstDir. */
static void createLinks(
    State & state,
    const std::filesystem::path & srcDir,
    Descriptor dstDirFd,
    const std::filesystem::path & dstDir,
    int priority)
{
    DirectoryIterator srcFiles;

    try {
        srcFiles = DirectoryIterator{srcDir};
    } catch (SystemError & e) {
        if (e.is(std::errc::not_a_directory)) {
            warn("not including %s in the user environment because it's not a directory", PathFmt(srcDir));
            return;
        }
        throw;
    }

    for (const auto & ent : srcFiles) {
        checkInterrupt();
        auto & dstName = ent.name;
        auto & name = dstName.path();
        if (name.string()[0] == '.')
            /* not matched by glob */
            continue;
        auto srcFile = (std::filesystem::path{srcDir} / name).string();
        auto dstFile = (std::filesystem::path{dstDir} / name).string();

        auto srcStOpt = maybeStat(srcFile.c_str());
        if (!srcStOpt) {
            warn("skipping dangling symlink '%s'", dstFile);
            continue;
        }
        auto & srcSt = *srcStOpt;

        /* The files below are special-cased to that they don't show
         * up in user profiles, either because they are useless, or
         * because they would cause pointless collisions (e.g., each
         * Python package brings its own
         * `$out/lib/pythonX.Y/site-packages/easy-install.pth'.)
         */
        if (hasSuffix(srcFile, "/propagated-build-inputs") || hasSuffix(srcFile, "/nix-support")
            || hasSuffix(srcFile, "/perllocal.pod") || hasSuffix(srcFile, "/info/dir") || hasSuffix(srcFile, "/log")
            || hasSuffix(srcFile, "/manifest.nix") || hasSuffix(srcFile, "/manifest.json"))
            continue;

        else if (S_ISDIR(srcSt.st_mode)) {
            auto dstStOpt = maybeFstatat(dstDirFd, dstName);
            if (dstStOpt) {
                auto & dstSt = *dstStOpt;
                if (S_ISDIR(dstSt.st_mode)) {
                    auto childDirFd = valueOr(
                        openDirectoryAt(dstDirFd, dstName),
                        NIX_THROW_NATIVE_SYS_ERROR_HANDLER("opening directory %s", PathFmt(dstFile)));
                    createLinks(state, srcFile, childDirFd.get(), dstFile, priority);
                    continue;
                } else if (S_ISLNK(dstSt.st_mode)) {
                    std::filesystem::path linkTarget = readLinkAt(dstDirFd, dstName);
                    auto target = canonPath(absPath(linkTarget, &dstDir), true);
                    if (!S_ISDIR(lstat(target).st_mode))
                        throw Error("collision between %1% and non-directory %2%", PathFmt(srcFile), PathFmt(target));
                    unlinkAt(dstDirFd, dstName);
                    auto childDirFd = createDirectory(dstDirFd, dstName, dstFile);
                    createLinks(state, target, childDirFd.get(), dstFile, state.priorities[dstFile]);
                    createLinks(state, srcFile, childDirFd.get(), dstFile, priority);
                    continue;
                }
            }
        }

        else {
            auto dstStOpt = maybeFstatat(dstDirFd, dstName);
            if (dstStOpt) {
                auto & dstSt = *dstStOpt;
                if (S_ISLNK(dstSt.st_mode)) {
                    auto prevPriority = state.priorities[dstFile];
                    if (prevPriority == priority)
                        throw BuildEnvFileConflictError(
                            std::filesystem::path{readLinkAt(dstDirFd, dstName)}, srcFile, priority);
                    if (prevPriority < priority)
                        continue;
                    unlinkAt(dstDirFd, dstName);
                } else if (S_ISDIR(dstSt.st_mode))
                    throw Error("collision between non-directory '%1%' and directory '%2%'", srcFile, dstFile);
            }
        }

        /* `srcSt` is the status of what the symlink will point to, which
           tells Windows which kind of symlink to create. */
        OsString target = std::filesystem::path{srcFile}.native();
        if (S_ISDIR(srcSt.st_mode))
            createDirectorySymlinkAt(dstDirFd, dstName, target);
        else
            createFileSymlinkAt(dstDirFd, dstName, target);
        state.priorities[dstFile] = priority;
        state.symlinks++;
    }
}

void buildProfile(Descriptor outFd, const std::filesystem::path & out, Packages && pkgs)
{
    State state;

    std::set<std::filesystem::path> done, postponed;

    auto addPkg = [&](const std::filesystem::path & pkgDir, int priority) {
        if (!done.insert(pkgDir).second)
            return;
        createLinks(state, pkgDir, outFd, out, priority);

        try {
            for (const auto & p : tokenizeString<std::vector<std::string>>(
                     readFile(pkgDir / "nix-support" / "propagated-user-env-packages"), " \n"))
                if (!done.count(p))
                    postponed.insert(p);
        } catch (SystemError & e) {
            if (!e.is(std::errc::no_such_file_or_directory) && !e.is(std::errc::not_a_directory))
                throw;
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
        std::set<std::filesystem::path> pkgDirs;
        postponed.swap(pkgDirs);
        for (const auto & pkgDir : pkgDirs)
            addPkg(pkgDir, priorityCounter++);
    }

    debug("created %d symlinks in user environment", state.symlinks);
}

static void builtinBuildenv(const BuiltinBuilderContext & ctx)
{
    auto getAttr = [&](const std::string & name) {
        auto i = ctx.drv.env.find(name);
        if (i == ctx.drv.env.end())
            throw Error("attribute '%s' missing", name);
        return i->second;
    };

    auto out = ctx.outputs.at("out");
    createDirs(out);
    auto outFd = valueOr(openDirectory(out), NIX_THROW_NATIVE_SYS_ERROR_HANDLER("opening directory %s", PathFmt(out)));

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

            for (size_t n{0}; n < outputs; n++) {
                pkgs.emplace_back(std::move(*itemIt++), active, priority);
            }
        }
    }

    buildProfile(outFd.get(), out, std::move(pkgs));

    createFileSymlinkAt(
        outFd.get(),
        OsFilename::fromPathAsserting("manifest.nix"),
        std::filesystem::path{getAttr("manifest")}.native());
}

static RegisterBuiltinBuilder registerBuildenv("buildenv", builtinBuildenv);

} // namespace nix
