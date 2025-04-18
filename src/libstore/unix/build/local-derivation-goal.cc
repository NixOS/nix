#include "nix/store/build/local-derivation-goal.hh"
#include "nix/store/local-store.hh"
#include "nix/util/processes.hh"
#include "nix/store/build/worker.hh"
#include "nix/util/util.hh"
#include "nix/store/restricted-store.hh"
#include "nix/store/build/derivation-builder.hh"

#include <sys/un.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>

#include "store-config-private.hh"

#if HAVE_STATVFS
#include <sys/statvfs.h>
#endif

#include <pwd.h>
#include <grp.h>

namespace nix {

/**
 * This hooks up `DerivationBuilder` to the scheduler / goal machinary.
 *
 * @todo Eventually, this shouldn't exist, because `DerivationGoal` can
 * just choose to use `DerivationBuilder` or its remote-building
 * equalivalent directly, at the "value level" rather than "class
 * inheritance hierarchy" level.
 */
struct LocalDerivationGoal : DerivationGoal, DerivationBuilderCallbacks
{
    std::unique_ptr<DerivationBuilder> builder;

    LocalDerivationGoal(const StorePath & drvPath,
        const OutputsSpec & wantedOutputs, Worker & worker,
        BuildMode buildMode)
        : DerivationGoal{drvPath, wantedOutputs, worker, buildMode}
    {}

    LocalDerivationGoal(const StorePath & drvPath, const BasicDerivation & drv,
        const OutputsSpec & wantedOutputs, Worker & worker,
        BuildMode buildMode = bmNormal)
        : DerivationGoal{drvPath, drv, wantedOutputs, worker, buildMode}
    {}

    virtual ~LocalDerivationGoal() override;

    /**
     * The additional states.
     */
    Goal::Co tryLocalBuild() override;

    bool isReadDesc(int fd) override;

    /**
     * Forcibly kill the child process, if any.
     *
     * Called by destructor, can't be overridden
     */
    void killChild() override final;

    void childStarted() override;
    void childTerminated() override;

    void noteHashMismatch(void) override;
    void noteCheckMismatch(void) override;

    void markContentsGood(const StorePath &) override;

    // Fake overrides to instantiate identically-named virtual methods

    Path openLogFile() override {
        return DerivationGoal::openLogFile();
    }
    void closeLogFile() override {
        DerivationGoal::closeLogFile();
    }
    SingleDrvOutputs assertPathValidity() override {
        return DerivationGoal::assertPathValidity();
    }
    void appendLogTailErrorMsg(std::string & msg) override {
        DerivationGoal::appendLogTailErrorMsg(msg);
    }
};

std::shared_ptr<DerivationGoal> makeLocalDerivationGoal(
    const StorePath & drvPath,
    const OutputsSpec & wantedOutputs, Worker & worker,
    BuildMode buildMode)
{
    return std::make_shared<LocalDerivationGoal>(drvPath, wantedOutputs, worker, buildMode);
}

std::shared_ptr<DerivationGoal> makeLocalDerivationGoal(
    const StorePath & drvPath, const BasicDerivation & drv,
    const OutputsSpec & wantedOutputs, Worker & worker,
    BuildMode buildMode)
{
    return std::make_shared<LocalDerivationGoal>(drvPath, drv, wantedOutputs, worker, buildMode);
}


LocalDerivationGoal::~LocalDerivationGoal()
{
    /* Careful: we should never ever throw an exception from a
       destructor. */
    if (builder) {
        try { builder->deleteTmpDir(false); } catch (...) { ignoreExceptionInDestructor(); }
    }
    try { killChild(); } catch (...) { ignoreExceptionInDestructor(); }
    if (builder) {
        try { builder->stopDaemon(); } catch (...) { ignoreExceptionInDestructor(); }
    }
}


void LocalDerivationGoal::killChild()
{
    if (builder && builder->pid != -1) {
        worker.childTerminated(this);

        /* If we're using a build user, then there is a tricky race
           condition: if we kill the build user before the child has
           done its setuid() to the build user uid, then it won't be
           killed, and we'll potentially lock up in pid.wait().  So
           also send a conventional kill to the child. */
        ::kill(-builder->pid, SIGKILL); /* ignore the result */

        builder->killSandbox(true);

        builder->pid.wait();
    }

    DerivationGoal::killChild();
}


void LocalDerivationGoal::childStarted()
{
    assert(builder);
    worker.childStarted(shared_from_this(), {builder->builderOut.get()}, true, true);
}

void LocalDerivationGoal::childTerminated()
{
    worker.childTerminated(this);
}

void LocalDerivationGoal::noteHashMismatch()
{
    worker.hashMismatch = true;
}


void LocalDerivationGoal::noteCheckMismatch()
{
    worker.checkMismatch = true;
}


void LocalDerivationGoal::markContentsGood(const StorePath & path)
{
    worker.markContentsGood(path);
}


Goal::Co LocalDerivationGoal::tryLocalBuild()
{
    assert(!hook);

    unsigned int curBuilds = worker.getNrLocalBuilds();
    if (curBuilds >= settings.maxBuildJobs) {
        outputLocks.unlock();
        co_await waitForBuildSlot();
        co_return tryToBuild();
    }

    if (!builder) {
        /* If we have to wait and retry (see below), then `builder` will
           already be created, so we don't need to create it again. */
        builder = makeDerivationBuilder(
            worker.store,
            static_cast<DerivationBuilderCallbacks &>(*this),
            DerivationBuilderParams {
                DerivationGoal::drvPath,
                DerivationGoal::buildMode,
                DerivationGoal::buildResult,
                *DerivationGoal::drv,
                DerivationGoal::parsedDrv.get(),
                *DerivationGoal::drvOptions,
                DerivationGoal::inputPaths,
                DerivationGoal::initialOutputs,
            });
    }

    if (!builder->prepareBuild()) {
        if (!actLock)
            actLock = std::make_unique<Activity>(*logger, lvlWarn, actBuildWaiting,
                fmt("waiting for a free build user ID for '%s'", Magenta(worker.store.printStorePath(drvPath))));
        co_await waitForAWhile();
        co_return tryLocalBuild();
    }

    actLock.reset();

    try {

        /* Okay, we have to build. */
        builder->startBuilder();

    } catch (BuildError & e) {
        outputLocks.unlock();
        builder->buildUser.reset();
        worker.permanentFailure = true;
        co_return done(BuildResult::InputRejected, {}, std::move(e));
    }

    started();
    co_await Suspend{};

    trace("build done");

    auto res = builder->unprepareBuild();
    // N.B. cannot use `std::visit` with co-routine return
    if (auto * ste = std::get_if<0>(&res)) {
        outputLocks.unlock();
        co_return done(std::move(ste->first), {}, std::move(ste->second));
    } else if (auto * builtOutputs = std::get_if<1>(&res)) {
        /* It is now safe to delete the lock files, since all future
           lockers will see that the output paths are valid; they will
           not create new lock files with the same names as the old
           (unlinked) lock files. */
        outputLocks.setDeletion(true);
        outputLocks.unlock();
        co_return done(BuildResult::Built, std::move(*builtOutputs));
    } else {
        unreachable();
    }
}


bool LocalDerivationGoal::isReadDesc(int fd)
{
    return (hook && DerivationGoal::isReadDesc(fd)) ||
        (!hook && builder && fd == builder->builderOut.get());
}

}
