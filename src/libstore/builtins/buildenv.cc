#include "nix/store/builtins/buildenv.hh"
#include "nix/store/builtins.hh"
#include "nix/store/derivations.hh"
#include "nix/util/archive.hh"
#include "nix/util/signals.hh"
#include "nix/util/system-error.hh"

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
    MemorySourceAccessor & out;
    std::map<CanonPath, int> priorities;
    unsigned long symlinks = 0;
};

} // namespace

/* For each activated package, create symlinks */
static void
createLinks(State & state, const std::filesystem::path & srcDir, const CanonPath & dstDir, int priority, size_t depth)
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
        auto name = ent.path().filename();
        if (name.string()[0] == '.')
            /* not matched by glob */
            continue;
        auto srcFile = srcDir / name;
        auto dstFile = dstDir / name.string();

        auto srcStOpt = maybeStat(srcFile);
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
        if (name == "propagated-build-inputs" || name == "nix-support" || name == "perllocal.pod"
            || (name == "dir" && srcDir.filename() == "info") || name == "log" || name == "manifest.nix"
            || name == "manifest.json")
            continue;

        if (depth + 1 >= narMaxDepth)
            throw Error("path '%s' exceeds maximum NAR directory depth of %d", dstFile, narMaxDepth);

        if (S_ISDIR(srcSt.st_mode)) {
            if (auto * dst = state.out.open(dstFile, std::nullopt)) {
                if (std::holds_alternative<MemorySourceAccessor::File::Directory>(dst->raw)) {
                    createLinks(state, srcFile, dstFile, priority, depth + 1);
                    continue;
                } else if (auto * link = std::get_if<MemorySourceAccessor::File::Symlink>(&dst->raw)) {
                    auto target = canonPath(link->target, true);
                    if (!S_ISDIR(lstat(target).st_mode))
                        throw Error("collision between %1% and non-directory %2%", PathFmt(srcFile), PathFmt(target));
                    dst->raw = MemorySourceAccessor::File::Directory{};
                    createLinks(state, target, dstFile, state.priorities[dstFile], depth + 1);
                    createLinks(state, srcFile, dstFile, priority, depth + 1);
                    continue;
                }
            }
        }

        else {
            if (auto * dst = state.out.open(dstFile, std::nullopt)) {
                if (auto * link = std::get_if<MemorySourceAccessor::File::Symlink>(&dst->raw)) {
                    auto prevPriority = state.priorities[dstFile];
                    if (prevPriority == priority)
                        throw BuildEnvFileConflictError(link->target, srcFile, priority);
                    if (prevPriority < priority)
                        continue;
                } else if (std::holds_alternative<MemorySourceAccessor::File::Directory>(dst->raw))
                    throw Error("collision between non-directory %1% and directory %2%", PathFmt(srcFile), dstFile);
            }
        }

        MemorySink{state.out}.createSymlink(dstFile, srcFile.string());
        state.priorities[dstFile] = priority;
        state.symlinks++;
    }
}

ref<MemorySourceAccessor> buildProfile(Packages && pkgs)
{
    auto out = make_ref<MemorySourceAccessor>();
    out->open(CanonPath::root, MemorySourceAccessor::File::Directory{});

    State state{.out = *out};

    std::set<std::filesystem::path> done, postponed;

    auto addPkg = [&](const std::filesystem::path & pkgDir, int priority) {
        if (!done.insert(pkgDir).second)
            return;
        createLinks(state, pkgDir, CanonPath::root, priority, 0);

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

    return out;
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

    auto profile = buildProfile(std::move(pkgs));

    RestoreSink sink(/*startFsync=*/false);
    sink.dstPath = out;
    copyRecursive(*profile, CanonPath::root, sink, CanonPath::root);

    createSymlink(getAttr("manifest"), out + "/manifest.nix");
}

static RegisterBuiltinBuilder registerBuildenv("buildenv", builtinBuildenv);

} // namespace nix
