#pragma once

#include "unix-derivation-builder-impl.hh"
#include "chroot.hh"

namespace nix {

struct ChrootDerivationBuilder : virtual UnixDerivationBuilderImpl
{
private:
    void anchor() override;
public:
    ChrootDerivationBuilder(
        LocalStore & store, std::shared_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params)
        : UnixDerivationBuilderImpl{store, std::move(miscMethods), std::move(params)}
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

    inline bool needsHashRewrite() override
    {
        return false;
    }

    void setBuildTmpDir() override;

    std::filesystem::path tmpDirInSandbox() override;

    virtual gid_t sandboxGid();

    void prepareSandbox() override;

    Strings getPreBuildHookArgs() override;

    std::filesystem::path realPathInHost(const std::filesystem::path & p) override;

    void cleanupBuild(bool force) override;

    std::pair<std::filesystem::path, std::filesystem::path> addDependencyPrep(const StorePath & path);
};

} // namespace nix
