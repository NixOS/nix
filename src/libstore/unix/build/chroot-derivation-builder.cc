#ifdef __linux__

#  include "chroot.hh"

namespace nix {

struct ChrootDerivationBuilder : virtual DerivationBuilderImpl
{
    ChrootDerivationBuilder(
        LocalStore & store, std::unique_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params)
        : DerivationBuilderImpl{store, std::move(miscMethods), std::move(params)}
    {
    }

    /**
     * The chroot root directory.
     */
    std::filesystem::path chrootRootDir;

    /**
     * RAII cleanup for the chroot directory.
     */
    std::optional<AutoDelete> autoDelChroot;

    PathsInChroot pathsInChroot;

    bool needsHashRewrite() override
    {
        return false;
    }

    void setBuildTmpDir() override
    {
        /* If sandboxing is enabled, put the actual TMPDIR underneath
           an inaccessible root-owned directory, to prevent outside
           access.

           On macOS, we don't use an actual chroot, so this isn't
           possible. Any mitigation along these lines would have to be
           done directly in the sandbox profile. */
        tmpDir = topTmpDir / "build";
        createDir(tmpDir, 0700);
    }

    std::filesystem::path tmpDirInSandbox() override
    {
        /* In a sandbox, for determinism, always use the same temporary
           directory. */
        return store.config->getLocalSettings().sandboxBuildDir.get();
    }

    virtual gid_t sandboxGid()
    {
        return buildUser->getGID();
    }

    void prepareSandbox() override
    {
        // Start with the default sandbox paths
        pathsInChroot = getPathsInSandbox();

        for (auto & i : inputPaths) {
            auto p = store.printStorePath(i);
            pathsInChroot.insert_or_assign(p, ChrootPath{.source = store.toRealPath(p)});
        }

        /* If we're repairing, checking or rebuilding part of a
           multiple-outputs derivation, it's possible that we're
           rebuilding a path that is in settings.sandbox-paths
           (typically the dependencies of /bin/sh).  Throw them
           out. */
        for (auto & i : drv.outputsAndOptPaths(store)) {
            /* If the name isn't known a priori (i.e. floating
               content-addressing derivation), the temporary location we use
               should be fresh.  Freshness means it is impossible that the path
               is already in the sandbox, so we don't need to worry about
               removing it.  */
            if (i.second.second)
                pathsInChroot.erase(store.printStorePath(*i.second.second));
        }

        // Set up chroot parameters
        BuildChrootParams params{
            .chrootParentDir = store.toRealPath(drvPath) + ".chroot",
            .useUidRange = drvOptions.useUidRange(drv),
            .isSandboxed = derivationType.isSandboxed(),
            .buildUser = buildUser.get(),
            .storeDir = store.storeDir,
            .chownToBuilder = [this](const std::filesystem::path & path) { this->chownToBuilder(path); },
            .getSandboxGid = [this]() { return this->sandboxGid(); },
        };

        // Create the chroot
        auto [rootDir, cleanup] = setupBuildChroot(params);
        chrootRootDir = std::move(rootDir);
        autoDelChroot.emplace(std::move(cleanup));
    }

    Strings getPreBuildHookArgs() override
    {
        assert(!chrootRootDir.empty());
        return Strings({store.printStorePath(drvPath), chrootRootDir.native()});
    }

    std::filesystem::path realPathInHost(const std::filesystem::path & p) override
    {
        // FIXME: why the needsHashRewrite() conditional?
        return !needsHashRewrite() ? chrootRootDir / p.relative_path()
                                   : std::filesystem::path(store.toRealPath(p.native()));
    }

    void cleanupBuild(bool force) override
    {
        DerivationBuilderImpl::cleanupBuild(force);

        if (autoDelChroot) {
            /* Move paths out of the chroot for easier debugging of
               build failures. */
            if (!force && buildMode == bmNormal)
                for (auto & [_, status] : initialOutputs) {
                    if (!status.known)
                        continue;
                    if (buildMode != bmCheck && status.known->isValid())
                        continue;
                    std::filesystem::path p = store.toRealPath(status.known->path);
                    std::filesystem::path chrootPath = chrootRootDir / p.relative_path();
                    if (pathExists(chrootPath))
                        std::filesystem::rename(chrootPath, p);
                }

            autoDelChroot.reset();
        }
    }

    std::pair<std::filesystem::path, std::filesystem::path> addDependencyPrep(const StorePath & path)
    {
        DerivationBuilderImpl::addDependencyImpl(path);

        debug("materialising '%s' in the sandbox", store.printStorePath(path));

        std::filesystem::path source = store.toRealPath(path);
        std::filesystem::path target =
            chrootRootDir / std::filesystem::path(store.printStorePath(path)).relative_path();

        if (pathExists(target)) {
            // There is a similar debug message in doBind, so only run it in this block to not have double messages.
            debug("bind-mounting %s -> %s", PathFmt(target), PathFmt(source));
            throw Error("store path '%s' already exists in the sandbox", store.printStorePath(path));
        }

        return {source, target};
    }
};

} // namespace nix

#endif
