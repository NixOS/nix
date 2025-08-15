#pragma once
///@file

#include "nix/store/build/derivation-builder.hh"
#include "nix/util/file-system.hh"
#include "nix/store/local-store.hh"
#include "nix/store/user-lock.hh"
#include "nix/store/parsed-derivations.hh"
#include "nix/store/restricted-store.hh"
#include "nix/util/processes.hh"

namespace nix {

/**
 * Common types and definitions for Unix derivation builders
 */

/**
 * Stuff we need to pass to initChild().
 */
struct ChrootPath
{
    Path source;
    bool optional;

    ChrootPath(Path source = "", bool optional = false)
        : source(source)
        , optional(optional)
    {
    }
};

typedef std::map<Path, ChrootPath> PathsInChroot; // maps target path to source path

/**
 * Handle the current exception by formatting an error and optionally
 * sending it to the parent process.
 */
void handleChildException(bool sendException);

/**
 * Base implementation class for Unix derivation builders.
 * This class is implemented in derivation-builder.cc.
 */
class DerivationBuilderImpl : public DerivationBuilder, public DerivationBuilderParams
{
public:
    DerivationBuilderImpl(
        Store & store, std::unique_ptr<DerivationBuilderCallbacks> miscMethods, DerivationBuilderParams params);

    // Virtual methods that can be overridden by platform-specific builders
    virtual bool needsHashRewrite();
    virtual void setBuildTmpDir();
    virtual Path tmpDirInSandbox();
    virtual void prepareSandbox();
    virtual Strings getPreBuildHookArgs();
    virtual Path realPathInSandbox(const Path & p);
    virtual void cleanupBuild();

    // Implement base class interface
    bool prepareBuild() override;
    void startBuilder() override;
    std::variant<std::pair<BuildResult::Status, Error>, SingleDrvOutputs> unprepareBuild() override;
    void deleteTmpDir(bool force) override;
    void killSandbox(bool getStats) override;
    void stopDaemon() override;
    void addDependency(const StorePath & path) override;

protected:
    // Protected members that derived classes need access to
    Store & store;
    std::unique_ptr<DerivationBuilderCallbacks> miscMethods;
    std::unique_ptr<UserLock> buildUser;
    Path tmpDir;
    Path topTmpDir;
    AutoCloseFD tmpDirFd;
    const DerivationType derivationType;
    typedef StringMap Environment;
    Environment env;
    StringMap inputRewrites, outputRewrites;
    typedef std::map<StorePath, StorePath> RedirectedOutputs;
    RedirectedOutputs redirectedOutputs;
    OutputPathMap scratchOutputs;
    AutoCloseFD daemonSocket;
    std::thread daemonThread;
    std::vector<std::thread> daemonWorkerThreads;

    // Protected methods that derived classes can call
    PathsInChroot getPathsInSandbox();
    virtual void prepareUser();
    virtual void startChild();
    void openSlave();
    void processSandboxSetupMessages();
    void chownToBuilder(const Path & path);
    void chownToBuilder(int fd, const Path & path);
    void writeBuilderFile(const std::string & name, std::string_view contents);
    void runChild();

    // DerivationBuilder interface
    const StorePathSet & originalPaths() override;
    bool isAllowed(const StorePath & path) override;
    bool isAllowed(const DrvOutput & id) override;

protected:
    // Protected implementation details that derived classes may need
    virtual void setUser();

private:
    // Private implementation details
    void initEnv();
    void writeStructuredAttrs();
    void startDaemon();
    virtual std::unique_ptr<UserLock> getBuildUser();
    virtual void enterChroot();
    virtual void execBuilder(const Strings & args, const Strings & envStrs);
    SingleDrvOutputs registerOutputs();
    void checkOutputs(const std::map<std::string, ValidPathInfo> & outputs);
    bool decideWhetherDiskFull();
    StorePath makeFallbackPath(const StorePath & path);
    StorePath makeFallbackPath(OutputNameView outputName);

    static const Path homeDir;
};

} // namespace nix