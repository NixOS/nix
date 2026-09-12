#include "nix/store/build/derivation-building-goal.hh"
#include "nix/store/build/derivation-env-desugar.hh"
#include "nix/store/restricted-store.hh"
#include "nix/store/daemon.hh"
#include "nix/store/build/derivation-builder.hh"
#include "nix/store/remote-store.hh"
#include "nix/store/legacy-ssh-store.hh"
#include "nix/store/store-open.hh"
#include "nix/util/hash.hh"
#include "nix/util/fun.hh"
#include "nix/util/finally.hh"
#include "nix/util/processes.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/config-global.hh"
#include "nix/store/build/worker.hh"
#include "nix/util/util.hh"
#include "nix/util/compression.hh"
#include "nix/store/common-protocol.hh"
#include "nix/store/common-protocol-impl.hh"
#include "nix/store/local-store.hh" // TODO remove, along with remaining downcasts
#include "nix/store/outputs-query.hh"
#include "nix/store/globals.hh"
#include "nix/store/machines.hh"
#include "nix/util/current-process.hh"

#include <chrono>
#include <algorithm>
#include <array>

#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#ifdef __APPLE__
#  include <sys/time.h>
#endif

#include <boost/asio/deferred.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/parallel_group.hpp>
#ifndef _WIN32
#  include <boost/asio/posix/stream_descriptor.hpp>
#else
#  include <boost/asio/windows/stream_handle.hpp>
#endif

#include <deque>

#include "nix/util/strings.hh"

namespace nix {

DerivationBuildingGoal::DerivationBuildingGoal(
    const StorePath & drvPath, ref<const BasicDerivation> drv, Worker & worker, BuildMode buildMode)
    : Goal(worker, init())
    , drvPath(drvPath)
    , drv{std::move(drv)}
    , buildMode(buildMode)
{
    name = fmt("building derivation '%s'", worker.store.printStorePath(drvPath));
    trace("created");

    /* Prevent the .chroot directory from being
       garbage-collected. (See isActiveTempFile() in gc.cc.) */
    worker.store.addTempRoot(this->drvPath);
}

DerivationBuildingGoal::~DerivationBuildingGoal() = default;

std::string DerivationBuildingGoal::key()
{
    return "dd$" + std::string(drvPath.name()) + "$" + worker.store.printStorePath(drvPath);
}

template<typename InputsType>
std::string
showKnownOutputs(const StoreDirConfig & store, const derivation::Derivation<InputsType, derivation::Output> & drv)
{
    std::string msg;
    StorePathSet expectedOutputPaths;
    for (auto & i : outputsAndOptPaths(drv, store))
        if (i.second.second)
            expectedOutputPaths.insert(*i.second.second);
    if (!expectedOutputPaths.empty()) {
        msg += "\nOutput paths:";
        for (auto & p : expectedOutputPaths)
            msg += fmt("\n  %s", Magenta(store.printStorePath(p)));
    }
    return msg;
}

template std::string showKnownOutputs(const StoreDirConfig & store, const Derivation & drv);
template std::string showKnownOutputs(const StoreDirConfig & store, const BasicDerivation & drv);

namespace {

struct LogSink : Sink
{
    Activity & act;
    std::string currentLine;

    LogSink(Activity & act)
        : act(act)
    {
    }

    void operator()(std::string_view data) override
    {
        for (auto c : data) {
            if (c == '\n') {
                flushLine();
            } else {
                currentLine += c;
            }
        }
    }

    void flushLine()
    {
        act.result(resPostBuildLogLine, currentLine);
        currentLine.clear();
    }

    ~LogSink()
    {
        if (currentLine != "") {
            currentLine += '\n';
            flushLine();
        }
    }
};

} // namespace

struct PostBuildHookState
{
    const std::string hook;
    Activity act;
    std::unique_ptr<LogSink> sink;
    std::unique_ptr<Pipe> out;
    Pid pid;

    PostBuildHookState(Logger & logger, const std::string hook, const std::string drvPath)
        : hook(hook)
        , act(logger,
              lvlTalkative,
              actPostBuildHook,
              fmt("running post-build-hook '%s'", hook),
              std::to_array<Logger::Field>({drvPath}))
        , out(std::make_unique<Pipe>())
    {
        out->create();
        sink = std::make_unique<LogSink>(act);
    }

    void complete()
    {
        if (int ret = pid.wait()) {
            throw Error("program \"%s\" %s", hook, statusToString(ret));
        }
    }
};

namespace {

/**
 * Output read from one of a child process's pipes.
 */
struct ChildOutput
{
    Descriptor fd;
    std::string data;
};

/**
 * End of file on one of a child process's pipes.
 */
struct ChildEOF
{
    Descriptor fd;
};

using ChildEvent = std::variant<ChildOutput, ChildEOF, TimedOut>;

/**
 * Timeouts to enforce while waiting for a child's output. Zero means
 * no limit.
 */
struct ChildTimeouts
{
    time_t maxSilentTime;
    time_t buildTimeout;
};

#ifndef _WIN32
using ChildStream = asio::posix::stream_descriptor;
#else
using ChildStream = asio::windows::stream_handle;
#endif

/**
 * Asynchronously reads the pipes of a child process, delivering one
 * @ref ChildEvent at a time from @ref next.
 *
 * The descriptors stay owned by the caller; they are only registered
 * with the reactor (or I/O completion port) for the lifetime of this
 * object. On Windows they must have been opened for overlapped I/O.
 */
class ChildEvents
{
    using Clock = std::chrono::steady_clock;

    /**
     * One pipe and the buffer its reads land in. Heap-allocated so that
     * the addresses stay stable while reads are in flight.
     */
    struct Stream
    {
        ChildStream stream;
        std::array<char, 4096> buf;

        Stream(asio::any_io_executor ex, Descriptor fd)
            : stream(ex, fd)
        {
        }

        ~Stream()
        {
            stream.release();
        }
    };

    std::vector<std::unique_ptr<Stream>> streams;
    std::optional<ChildTimeouts> timeouts;
    asio::steady_timer timer;
    Clock::time_point timeStarted = Clock::now();
    Clock::time_point lastOutput = timeStarted;

    /**
     * Events already received but not yet handed out by @ref next.
     */
    std::deque<ChildEvent> pending;

    /**
     * Whether a read error means that the child has closed its end.
     */
    static bool isEOF(const boost::system::error_code & ec)
    {
        if (ec == asio::error::eof)
            return true;
#ifndef _WIN32
        /* Reading the master side of a pseudoterminal fails with EIO once
           the child has closed the slave side. */
        return ec == boost::system::error_code(EIO, boost::system::system_category());
#else
        return ec == boost::system::error_code(ERROR_BROKEN_PIPE, boost::system::system_category());
#endif
    }

    /**
     * The nearest timeout deadline, paired with the number of seconds
     * of the limit that produced it.
     */
    std::optional<std::pair<Clock::time_point, time_t>> deadline() const
    {
        std::optional<std::pair<Clock::time_point, time_t>> res;
        if (!timeouts)
            return res;
        auto consider = [&](time_t seconds, Clock::time_point from) {
            if (seconds == 0)
                return;
            auto at = from + std::chrono::seconds(seconds);
            if (!res || at < res->first)
                res = {at, seconds};
        };
        consider(timeouts->maxSilentTime, lastOutput);
        consider(timeouts->buildTimeout, timeStarted);
        return res;
    }

    /**
     * Wait until at least one stream has produced output or EOF, or the
     * timeout has expired, and queue the corresponding events.
     *
     * All streams are read concurrently. Once one read completes the
     * others are cancelled, but a read that completed in the meantime
     * still has its data, so every completion is looked at rather than
     * just the first.
     */
    asio::awaitable<void> waitForEvents()
    {
        assert(!streams.empty());

        auto dl = deadline();
        /* Check explicitly, since a child that never stops writing would
           otherwise always win the race against the timer. */
        if (dl && Clock::now() >= dl->first) {
            pending.push_back(TimedOut(dl->second));
            co_return;
        }

        using ReadOp =
            decltype(streams.front()->stream.async_read_some(asio::buffer(streams.front()->buf), asio::deferred));
        std::vector<ReadOp> reads;
        for (auto & s : streams)
            reads.push_back(s->stream.async_read_some(asio::buffer(s->buf), asio::deferred));
        auto readAny = asio::experimental::make_parallel_group(std::move(reads))
                           .async_wait(asio::experimental::wait_for_one(), asio::deferred);

        timer.expires_at(dl ? dl->first : Clock::time_point::max());

        auto results =
            co_await asio::experimental::make_parallel_group(std::move(readAny), timer.async_wait(asio::deferred))
                .async_wait(asio::experimental::wait_for_one(), asio::use_awaitable);
        auto & readOrder = std::get<1>(results);
        auto & readErrors = std::get<2>(results);
        auto & readSizes = std::get<3>(results);
        auto & timerError = std::get<4>(results);

        auto now = Clock::now();
        std::vector<Descriptor> closed;
        for (auto idx : readOrder) {
            auto & s = *streams[idx];
            auto fd = s.stream.native_handle();
            auto & ec = readErrors[idx];
            if (auto n = readSizes[idx]) {
                lastOutput = now;
                pending.push_back(ChildOutput{fd, std::string(s.buf.data(), n)});
            }
            if (ec == asio::error::operation_aborted)
                continue;
            if (isEOF(ec)) {
                pending.push_back(ChildEOF{fd});
                closed.push_back(fd);
            } else if (ec)
                throw boost::system::system_error(ec, "reading from child process");
        }

        std::erase_if(
            streams, [&](auto & s) { return std::ranges::find(closed, s->stream.native_handle()) != closed.end(); });

        if (dl && !timerError)
            pending.push_back(TimedOut(dl->second));
    }

public:
    ChildEvents(asio::any_io_executor ex, const std::set<Descriptor> & fds, std::optional<ChildTimeouts> timeouts)
        : timeouts(timeouts)
        , timer(ex)
    {
        for (auto fd : fds)
            streams.push_back(std::make_unique<Stream>(ex, fd));
    }

    asio::awaitable<ChildEvent> next()
    {
        while (pending.empty())
            co_await waitForEvents();
        auto event = std::move(pending.front());
        pending.pop_front();
        co_return event;
    }
};

} // namespace

/* Only used on Unix; on Windows every call site throws instead. */
#ifndef _WIN32

/**
 * Run the post-build hook for `drvPath`, streaming its output to the
 * logger, and wait for it to finish.
 */
static asio::awaitable<void> runPostBuildHook(
    const WorkerSettings & workerSettings,
    const StoreDirConfig & store,
    Logger & logger,
    const StorePath & drvPath,
    const StorePathSet & outputPaths);
#endif

asio::awaitable<Goal::ExitCode> DerivationBuildingGoal::init()
{
    auto result = co_await tryToBuild();

    mcRunningBuilds.reset();

    auto exitCode = std::visit(
        overloaded{
            [&](const BuildResult::Success & success) {
                if (success.status == BuildResult::Success::Built)
                    worker.doneBuilds++;
                return ecSuccess;
            },
            [&](const BuildResult::Failure & failure) {
                worker.exitStatusFlags.updateFromStatus(failure.status);
                if (failure.status != BuildResult::Failure::DependencyFailed)
                    worker.failedBuilds++;
                return ecFailed;
            },
        },
        result);

    buildResult.inner = std::move(result);
    worker.updateProgress();
    co_return exitCode;
}

/**
 * RAII wrapper for build log file.
 * Constructor opens the log file, destructor closes it.
 */
struct LogFile
{
    AutoCloseFD fd;
    std::shared_ptr<BufferedSink> fileSink, sink;

    LogFile(Store & store, const StorePath & drvPath, const LogFileSettings & logSettings);
    ~LogFile();
};

struct LocalBuildRejection
{
    bool maxJobsZero = false;

    struct NoLocalStore
    {};

    /**
     * We have a local store, but we don't have an external derivation builder (which is fine), if we did, it'd be
     * fine because we would not care about platforms and features then. Since we don't, we either have the wrong
     * platform, or we are missing some system features.
     */
    struct WrongLocalStore
    {
        template<typename T>
        struct Pair
        {
            T derivation;
            T localStore;
        };

        std::optional<Pair<std::string>> badPlatform;
        std::optional<Pair<StringSet>> missingFeatures;
    };

    std::variant<NoLocalStore, WrongLocalStore> rejection;
};

static BuildError reject(const LocalBuildRejection & rejection, std::string_view thingCannotBuild)
{
    if (std::get_if<LocalBuildRejection::NoLocalStore>(&rejection.rejection))
        return BuildError(
            BuildResult::Failure::InputRejected,
            "Unable to build with a primary store that isn't a local store; "
            "either pass a different '--store' or enable remote builds.\n\n"
            "For more information check 'man nix.conf' and search for '/machines'.");

    auto & wrongStore = std::get<LocalBuildRejection::WrongLocalStore>(rejection.rejection);

    std::string msg = fmt("Cannot build '%s'.", Magenta(thingCannotBuild));

    if (rejection.maxJobsZero)
        msg += "\nReason: " ANSI_RED "local builds are disabled" ANSI_NORMAL
               " (max-jobs = 0)"
               "\nHint: set 'max-jobs' to a non-zero value to enable local builds, "
               "or configure remote builders via 'builders'";

    if (wrongStore.badPlatform)
        msg +=
            fmt("\nReason: " ANSI_RED "platform mismatch" ANSI_NORMAL
                "\nRequired system: '%s'"
                "\nCurrent system: '%s'",
                Magenta(wrongStore.badPlatform->derivation),
                Magenta(wrongStore.badPlatform->localStore));

    if (wrongStore.missingFeatures)
        msg +=
            fmt("\nReason: " ANSI_RED "missing system features" ANSI_NORMAL
                "\nRequired features: {%s}"
                "\nAvailable features: {%s}",
                concatStringsSep(", ", wrongStore.missingFeatures->derivation),
                concatStringsSep<StringSet>(", ", wrongStore.missingFeatures->localStore));

    if (wrongStore.badPlatform || wrongStore.missingFeatures) {
        // since aarch64-darwin has Rosetta 2, this user can actually run x86_64-darwin on their
        // hardware - we should tell them to run the command to install Rosetta
        if (wrongStore.badPlatform && wrongStore.badPlatform->derivation == "x86_64-darwin"
            && wrongStore.badPlatform->localStore == "aarch64-darwin")
            msg +=
                fmt("\nNote: run `%s` to run programs for x86_64-darwin",
                    Magenta("/usr/sbin/softwareupdate --install-rosetta && launchctl stop org.nixos.nix-daemon"));
    }

    return BuildError(BuildResult::Failure::InputRejected, std::move(msg));
}

/* At least one of the output paths could not be
   produced using a substitute.  So we have to build instead. */
/* Remote builders. This is what the build hook (`nix __build-remote`)
   used to do in a separate process. */

static std::string escapeUri(std::string uri)
{
    std::replace(uri.begin(), uri.end(), '/', '_');
    return uri;
}

static AutoCloseFD
openRemoteBuilderSlotLock(const std::filesystem::path & currentLoad, const Machine & m, uint64_t slot)
{
    return openLockFile(currentLoad / fmt("%s-%d", escapeUri(m.storeUri.render()), slot), true);
}

/**
 * A slot on a remote builder, held (via its lock file) until destroyed.
 */
struct RemoteBuilderSlot
{
    Machine & machine;
    AutoCloseFD lock;
};

/**
 * Some remote builder could do it, but all of them are busy right now.
 */
struct RemoteBuildPostponed
{};

/**
 * No remote builder can do it (or none is configured): build locally,
 * or fail.
 */
struct RemoteBuildDeclined
{};

using RemoteBuilderChoice = std::variant<RemoteBuilderSlot, RemoteBuildPostponed, RemoteBuildDeclined>;

/**
 * Pick the least loaded remote builder that can build `drvPath`, with
 * the same policy the build hook had.
 *
 * @param couldBuildLocally Whether a local build is possible at all
 * (platform, features, `max-jobs`). Only affects how loudly a decline
 * is reported.
 *
 * @param canBuildLocally Whether a local build could start right now.
 * If not, and some builder has the right type but is busy, the answer
 * is to postpone rather than decline.
 */
static RemoteBuilderChoice chooseRemoteBuilder(
    Worker & worker,
    const StorePath & drvPath,
    const std::string & neededSystem,
    const StringSet & requiredFeatures,
    bool couldBuildLocally,
    bool canBuildLocally)
{
    auto & machines = worker.machines();
    if (machines.empty())
        return RemoteBuildDeclined{};

    auto & currentLoad = worker.currentLoad;

    /* Error ignored here, will be caught later */
    std::error_code ec;
    std::filesystem::create_directory(currentLoad, ec);

    AutoCloseFD lock = openLockFile(currentLoad / "main-lock", true);
    lockFile(lock.get(), ltWrite, true);

    bool rightType = false;

    Machine * bestMachine = nullptr;
    AutoCloseFD bestSlotLock;
    uint64_t bestLoad = 0;
    for (auto & m : machines) {
        debug("considering building on remote machine '%s'", m.storeUri.render());

        if (!(m.enabled && m.systemSupported(neededSystem) && m.allSupported(requiredFeatures)
              && m.mandatoryMet(requiredFeatures)))
            continue;

        rightType = true;
        AutoCloseFD free;
        uint64_t load = 0;
        for (uint64_t slot = 0; slot < m.maxJobs; ++slot) {
            auto slotLock = openRemoteBuilderSlotLock(currentLoad, m, slot);
            if (lockFile(slotLock.get(), ltWrite, false)) {
                if (!free)
                    free = std::move(slotLock);
            } else
                ++load;
        }
        if (!free)
            continue;

        bool best = false;
        if (!bestSlotLock)
            best = true;
        else if (load / m.speedFactor < bestLoad / bestMachine->speedFactor)
            best = true;
        else if (load / m.speedFactor == bestLoad / bestMachine->speedFactor) {
            if (m.speedFactor > bestMachine->speedFactor)
                best = true;
            else if (m.speedFactor == bestMachine->speedFactor && load < bestLoad)
                best = true;
        }
        if (best) {
            bestLoad = load;
            bestSlotLock = std::move(free);
            bestMachine = &m;
        }
    }

    if (!bestSlotLock) {
        if (rightType && !canBuildLocally)
            return RemoteBuildPostponed{};

        // build the hint template.
        std::string errorText =
            "Failed to find a machine for remote build!\n"
            "derivation: %s\nrequired (system, features): (%s, [%s])";
        errorText += "\n%s available machines:";
        errorText += "\n(systems, maxjobs, supportedFeatures, mandatoryFeatures)";
        for (unsigned int i = 0; i < machines.size(); ++i)
            errorText += "\n([%s], %s, [%s], [%s])";

        // add the template values.
        auto error = HintFmt::fromFormatString(errorText);
        error % worker.store.printStorePath(drvPath) % neededSystem
            % concatStringsSep<StringSet>(", ", requiredFeatures) % machines.size();
        for (auto & m : machines)
            error % concatStringsSep<StringSet>(", ", m.systemTypes) % m.maxJobs
                % concatStringsSep<StringSet>(", ", m.supportedFeatures)
                % concatStringsSep<StringSet>(", ", m.mandatoryFeatures);
        printMsg(couldBuildLocally ? lvlChatty : lvlWarn, error.str());

        return RemoteBuildDeclined{};
    }

    /* Refresh the lock file's mtime, so it shows when the slot was last
       used. Cosmetic, so not bothered with on Windows. */
#if defined(__APPLE__)
    futimes(bestSlotLock.get(), NULL);
#elif !defined(_WIN32)
    futimens(bestSlotLock.get(), NULL);
#endif

    return RemoteBuilderSlot{*bestMachine, std::move(bestSlotLock)};
}

/**
 * Take the lock serialising uploads to `machine`. Blocks, so run it on
 * the thread pool.
 */
static AutoCloseFD lockUploadsTo(const std::filesystem::path & currentLoad, const Machine & machine)
{
    auto storeUri = machine.storeUri.render();

    AutoCloseFD uploadLock;
    auto setUpdateLock = [&](auto && fileName) {
        uploadLock = openLockFile(currentLoad / (escapeUri(fileName) + ".upload-lock"), true);
    };
    try {
        setUpdateLock(storeUri);
    } catch (SystemError & e) {
        if (!e.is(std::errc::filename_too_long))
            throw;
        // Try again hashing the store URL so we have a shorter path
        auto h = hashString(HashAlgorithm::MD5, storeUri);
        setUpdateLock(h.to_string(HashFormat::Base64, false));
    }

    Activity act(*logger, lvlTalkative, actUnknown, fmt("waiting for the upload lock to '%s'", storeUri));
    lockFile(uploadLock.get(), ltWrite, true);

    return uploadLock;
}

asio::awaitable<DerivationBuildingGoal::Result> DerivationBuildingGoal::tryToBuild()
{
    Goals waitees;

    /* Copy the input sources from the eval store to the build
       store.

       Note that some inputs might not be in the eval store because they
       are (resolved) derivation outputs in a resolved derivation. */
    if (&worker.evalStore != &worker.store) {
        RealisedPath::Set inputSrcs;
        for (auto & i : drv->inputs)
            if (worker.evalStore.isValidPath(i))
                inputSrcs.insert(i);
        copyClosure(worker.evalStore, worker.store, inputSrcs);
    }

    for (auto & i : drv->inputs) {
        if (worker.store.isValidPath(i))
            continue;
        if (!worker.settings.useSubstitutes)
            throw Error(
                "dependency '%s' of '%s' does not exist, and substitution is disabled",
                worker.store.printStorePath(i),
                worker.store.printStorePath(drvPath));
        waitees.insert(upcast_goal(worker.makePathSubstitutionGoal(i)));
    }

    co_await await(std::move(waitees));

    trace("all inputs realised");

    if (nrFailed != 0) {
        auto msg =
            fmt("Cannot build '%s'.\n"
                "Reason: " ANSI_RED "%d %s failed" ANSI_NORMAL ".",
                Magenta(worker.store.printStorePath(drvPath)),
                nrFailed,
                nrFailed == 1 ? "dependency" : "dependencies");
        msg += showKnownOutputs(worker.store, *drv);
        co_return BuildError(BuildResult::Failure::DependencyFailed, msg);
    }

    /* Gather information necessary for computing the closure and/or
       running the build hook. */

    /* Determine the full set of input paths. */

    StorePathSet inputPaths;
    worker.store.computeFSClosure(drv->inputs, inputPaths);

    debug("added input paths %s", concatMapStringsSep(", ", inputPaths, [&](auto & p) {
              return "'" + worker.store.printStorePath(p) + "'";
          }));

    /* Okay, try to build.  Note that here we don't wait for a build
       slot to become available, since we don't need one if there is a
       build hook. */

    auto drvOptions = [&] {
        try {
            return derivationOptionsFromStructuredAttrs(worker.store, drv->env, get(drv->structuredAttrs));
        } catch (Error & e) {
            e.addTrace({}, "while parsing derivation '%s'", worker.store.printStorePath(drvPath));
            throw;
        }
    }();

    std::map<std::string, InitialOutput> initialOutputs;

    /* Recheck at this point. In particular, whereas before we were
       given this information by the downstream goal, that cannot happen
       anymore if the downstream goal only cares about one output, but
       we care about all outputs. */
    for (auto & [outputName, _] : drv->outputs) {
        InitialOutput v;

        /* TODO we might want to also allow randomizing the paths
           for regular CA derivations, e.g. for sake of checking
           determinism. */
        if (type(*drv).isImpure()) {
            v.known = InitialOutputStatus{
                .path = StorePath::random(outputPathName(drv->name, outputName)),
                .status = PathStatus::Absent,
            };
        }

        initialOutputs.insert({
            outputName,
            std::move(v),
        });
    }
    checkPathValidity(initialOutputs);

    auto localBuildResult = [&]() -> std::variant<LocalBuildCapability, LocalBuildRejection> {
        bool maxJobsZero = worker.settings.maxBuildJobs.get() == 0;

        auto * localStoreP = dynamic_cast<LocalStore *>(&worker.store);
        if (!localStoreP)
            return LocalBuildRejection{.maxJobsZero = maxJobsZero, .rejection = LocalBuildRejection::NoLocalStore{}};

        /**
         * Now that we've decided we can't / won't do a remote build, check
         * that we can in fact build locally. First see if there is an
         * external builder for a "semi-local build". If there is, prefer to
         * use that. If there is not, then check if we can do a "true" local
         * build.
         */
        auto * ext = settings.getLocalSettings().findExternalDerivationBuilderIfSupported(*drv);

        if (ext)
            return LocalBuildCapability{*localStoreP, ext};

        using WrongLocalStore = LocalBuildRejection::WrongLocalStore;

        WrongLocalStore wrongStore;

        if (drv->platform != settings.thisSystem.get() && !settings.extraPlatforms.get().count(drv->platform)
            && !drv->isBuiltin())
            wrongStore.badPlatform = WrongLocalStore::Pair<std::string>{drv->platform, settings.thisSystem.get()};

        {
            auto required = drvOptions.getRequiredSystemFeatures(*drv);
            auto & available = worker.store.config.systemFeatures.get();
            if (std::ranges::any_of(required, [&](const std::string & f) { return !available.count(f); }))
                wrongStore.missingFeatures = WrongLocalStore::Pair<StringSet>{required, available};
        }

        if (maxJobsZero || wrongStore.badPlatform || wrongStore.missingFeatures)
            return LocalBuildRejection{.maxJobsZero = maxJobsZero, .rejection = std::move(wrongStore)};

        return LocalBuildCapability{*localStoreP, ext};
    }();

    /* A local build slot, kept across retries of the loop below so that
       waiting for one does not turn into a livelock with other goals. */
    std::optional<AsyncSemaphore::Handle> buildSlot;

    auto acquireResources = [&](PathLocks & outputLocks) -> asio::awaitable<bool> {
        trace("trying to build");

        /**
         * Output paths to acquire locks on, if known a priori.
         *
         * The locks are automatically released when the caller's `PathLocks` goes
         * out of scope, including on exception unwinding.  If we can't acquire the lock, then
         * continue; hopefully some other goal can start a build, and if not, the
         * main loop will sleep a few seconds and then retry this goal.
         */
        std::set<std::filesystem::path> lockFiles;
        /* FIXME: Should lock something like the drv itself so we don't build same
           CA drv concurrently */
        if (auto * localStore = dynamic_cast<LocalStore *>(&worker.store)) {
            /* If we aren't a local store, we might need to use the local store as
               a build remote, but that would cause a deadlock. */
            /* FIXME: Make it so we can use ourselves as a build remote even if we
               are the local store (separate locking for building vs scheduling? */
            /* FIXME: find some way to lock for scheduling for the other stores so
               a forking daemon with --store still won't farm out redundant builds.
               */
            for (auto & i : outputsAndOptPaths(*drv, worker.store)) {
                if (i.second.second)
                    lockFiles.insert(localStore->toRealPath(*i.second.second));
                else {
                    auto lockPath = localStore->toRealPath(drvPath);
                    lockPath += "." + i.first;
                    lockFiles.insert(std::move(lockPath));
                }
            }
        }

        if (!outputLocks.lockPaths(lockFiles, "", false)) {
            Activity act(
                *logger,
                lvlWarn,
                actBuildWaiting,
                fmt("waiting for lock on %s",
                    Magenta(concatMapStringsSep(", ", lockFiles, [](auto & p) { return "'" + p.string() + "'"; }))));

            /* Wait then try locking again, repeat until success (returned
               boolean is true). */
            do {
                co_await waitForAWhile();
            } while (!outputLocks.lockPaths(lockFiles, "", false));
        }

        /* Now check again whether the outputs are valid.  This is because
           another process may have started building in parallel.  After
           it has finished and released the locks, we can (and should)
           reuse its results.  (Strictly speaking the first check can be
           omitted, but that would be less efficient.)  Note that since we
           now hold the locks on the output paths, no other process can
           build this derivation, so no further checks are necessary. */
        auto [allValid, validOutputs] = checkPathValidity(initialOutputs);

        if (buildMode != bmCheck && allValid) {
            debug("skipping build of derivation '%s', someone beat us to it", worker.store.printStorePath(drvPath));
            outputLocks.setDeletion(true);
            outputLocks.unlock();
            co_return true;
        }

        /* If any of the outputs already exist but are not valid, delete
           them. */
        if (auto * localStore = dynamic_cast<LocalFSStore *>(&worker.store)) {
            for (auto & [_, status] : initialOutputs) {
                if (!status.known || status.known->isValid())
                    continue;
                auto storePath = status.known->path;
                debug("removing invalid path '%s'", worker.store.printStorePath(status.known->path));
                deletePath(localStore->toRealPath(storePath));
            }
        }

        co_return false;
    };

    /* Whether a local build is possible at all, as opposed to right now. */
    bool couldBuildLocally = std::holds_alternative<LocalBuildCapability>(localBuildResult);

    auto tryRemote = [&]() -> asio::awaitable<std::optional<Result>> {
        /* Remote builders get the derivation from our store, so it has
           to be there. */
        if (!worker.store.isValidPath(drvPath))
            co_return std::nullopt;

        std::unique_ptr<Activity> actWaiting;
        while (true) {
            PathLocks outputLocks;
            if (co_await acquireResources(outputLocks))
                co_return Result{BuildResult::Success{
                    .status = BuildResult::Success::AlreadyValid,
                    .builtOutputs = checkPathValidity(initialOutputs).second,
                }};

            auto choice = chooseRemoteBuilder(
                worker,
                drvPath,
                drv->platform,
                drvOptions.getRequiredSystemFeatures(*drv),
                couldBuildLocally,
                couldBuildLocally && (buildSlot || worker.buildSemaphore.canAcquireNow()));

            if (auto * slot = std::get_if<RemoteBuilderSlot>(&choice)) {
                /* The local build slot, if any, is not needed for a
                   remote build. */
                buildSlot.reset();
                actWaiting.reset();
                if (auto result = co_await buildRemotely(
                        slot->machine,
                        std::move(slot->lock),
                        inputPaths,
                        initialOutputs,
                        drvOptions,
                        std::move(outputLocks)))
                    co_return std::move(*result);
                /* That builder is unusable; pick another one. */
                continue;
            }

            if (std::holds_alternative<RemoteBuildDeclined>(choice))
                // We should do it ourselves.
                co_return std::nullopt;

            /* Postponed: some builder could do it, but they are all
               busy. Wait a while and try again. */
            if (!actWaiting)
                actWaiting = std::make_unique<Activity>(
                    *logger,
                    lvlWarn,
                    actBuildWaiting,
                    fmt("waiting for a machine to build '%s'", Magenta(worker.store.printStorePath(drvPath))));
            outputLocks.unlock();
            co_await waitForAWhile();
        }
    };

    auto tryBuildLocally = [&]() -> asio::awaitable<std::optional<LocalBuildOutcome>> {
        if (auto * cap = std::get_if<LocalBuildCapability>(&localBuildResult)) {
            PathLocks outputLocks;
            if (co_await acquireResources(outputLocks))
                co_return Result{BuildResult::Success{
                    .status = BuildResult::Success::AlreadyValid,
                    .builtOutputs = checkPathValidity(initialOutputs).second,
                }};

            co_return co_await buildLocally(
                *cap, inputPaths, initialOutputs, drvOptions, std::move(outputLocks), &buildSlot, std::nullopt);
        }

        co_return std::nullopt;
    };

    while (true) {
        std::optional<LocalBuildOutcome> local;

        if (buildMode != bmNormal) {
            // Check and repair modes operate on the state of this store specifically,
            // so they must always build locally.
            local = co_await tryBuildLocally();
        } else if (drvOptions.preferLocalBuild) {
            // Local is preferred, so try it first. If it's not available, fall back to a remote builder.
            local = co_await tryBuildLocally();
            if (!local)
                if (auto result = co_await tryRemote())
                    co_return std::move(*result);
        } else {
            // Default preference is a remote build: they tend to be faster and preserve local
            // resources for other tasks. Fall back to local if no remote is available.
            if (auto result = co_await tryRemote())
                co_return std::move(*result);
            local = co_await tryBuildLocally();
        }

        if (!local)
            break;
        if (auto * result = std::get_if<Result>(&*local))
            co_return std::move(*result);
    }

    std::string storePath = worker.store.printStorePath(drvPath);
    auto * rejection = std::get_if<LocalBuildRejection>(&localBuildResult);
    assert(rejection);
    co_return reject(*rejection, storePath);
}

/**
 * Run a blocking function on the worker's thread pool, resuming on the
 * strand with its result.
 */
template<typename F>
static asio::awaitable<std::invoke_result_t<F>> onThreadPool(asio::thread_pool & pool, F f)
{
    using R = std::invoke_result_t<F>;
    co_return co_await asio::co_spawn(
        pool, [f = std::move(f)]() mutable -> asio::awaitable<R> { co_return f(); }, asio::use_awaitable);
}

asio::awaitable<void> DerivationBuildingGoal::copyOutputsFromBuilder(
    Store & builderStore, std::string_view builderName, const SingleDrvOutputs & outputs)
{
    StorePathSet missingPaths;
    std::set<Realisation> missingRealisations;

    bool wantRealisations =
        experimentalFeatureSettings.isEnabled(Xp::CaDerivations) && !derivation::type(*drv).hasKnownOutputPaths();

    for (auto & [outputName, realisation] : outputs) {
        if (!worker.store.isValidPath(realisation.outPath))
            missingPaths.insert(realisation.outPath);
        DrvOutput id{drvPath, outputName};
        if (wantRealisations && !worker.store.queryRealisation(id))
            missingRealisations.insert({realisation, id});
    }

    if (!missingPaths.empty()) {
        /* We hold the locks on the output paths ourselves (see
           `acquireResources`), so `LocalStore::addToStore` must not try
           to take them again. */
        auto * localStore = dynamic_cast<LocalStore *>(&worker.store);
        if (localStore) {
            auto locksHeld(localStore->locksHeld.lock());
            for (auto & path : missingPaths)
                locksHeld->insert(worker.store.printStorePath(path));
        }
        Finally release([&] {
            if (localStore) {
                auto locksHeld(localStore->locksHeld.lock());
                for (auto & path : missingPaths)
                    locksHeld->erase(worker.store.printStorePath(path));
            }
        });

        co_await onThreadPool(worker.getThreadPool(), [&] {
            Activity act(*logger, lvlTalkative, actUnknown, fmt("copying outputs from '%s'", builderName));
            copyPaths(builderStore, worker.store, missingPaths, NoRepair, NoCheckSigs, NoSubstitute);
        });
    }

    // XXX: Should be done as part of `copyPaths`
    for (auto & realisation : missingRealisations)
        worker.store.registerDrvOutput(realisation, NoCheckSigs);
}

asio::awaitable<std::optional<DerivationBuildingGoal::Result>> DerivationBuildingGoal::buildRemotely(
    Machine & machine,
    AutoCloseFD slotLock,
    StorePathSet inputPaths,
    std::map<std::string, InitialOutput> initialOutputs,
    DerivationOptions<StorePath> drvOptions,
    PathLocks outputLocks)
{
    auto storeUri = machine.storeUri.render();
    auto & pool = worker.getThreadPool();

    /* Connect. A failure disables the builder and lets the caller pick
       another one. */
    std::shared_ptr<Store> builderStore;
#ifndef _WIN32
    /* `ssh://` stores can send the remote build log (and SSH's own
       errors) to a descriptor of ours. */
    Pipe logPipe;
#endif
    try {
        auto storeRef = machine.completeStoreReference();
#ifndef _WIN32
        if (auto * generic = std::get_if<StoreReference::Specified>(&storeRef.variant);
            generic && generic->scheme == "ssh") {
            logPipe.create();
            storeRef.params["log-fd"] = std::to_string(logPipe.writeSide.get());
        }
#endif
        Activity act(*logger, lvlTalkative, actUnknown, fmt("connecting to '%s'", storeUri));
        builderStore = co_await onThreadPool(pool, [&]() -> std::shared_ptr<Store> {
            auto s = openStore(StoreReference(storeRef));
            s->connect();
            return s.get_ptr();
        });
    } catch (std::exception & e) {
        std::string msg;
#ifndef _WIN32
        if (logPipe.readSide)
            msg = chomp(drainFD(logPipe.readSide.get(), {.block = false}));
#endif
        printError("cannot build on '%s': %s%s", storeUri, e.what(), msg.empty() ? "" : ": " + msg);
        machine.enabled = false;
        co_return std::nullopt;
    }

    auto substitute = worker.settings.buildersUseSubstitutes ? Substitute : NoSubstitute;

    /* Copy the inputs over, one upload per builder at a time. */
    co_await onThreadPool(pool, [&] {
        auto uploadLock = lockUploadsTo(worker.currentLoad, machine);
        Activity act(*logger, lvlTalkative, actUnknown, fmt("copying dependencies to '%s'", storeUri));
        copyPaths(worker.store, *builderStore, inputPaths, NoRepair, NoCheckSigs, substitute);
    });

    /* A local store used as a builder: no need for another process (or
       another scheduler) at all, just build in that store ourselves. */
    if (auto * localStore = dynamic_cast<LocalStore *>(&*builderStore)) {
        auto outcome = co_await buildLocally(
            LocalBuildCapability{
                *localStore, settings.getLocalSettings().findExternalDerivationBuilderIfSupported(*drv)},
            inputPaths,
            initialOutputs,
            drvOptions,
            std::move(outputLocks),
            /* No local build slot is needed; the builder's own slot limits us. */
            nullptr,
            storeUri);
        auto * result = std::get_if<Result>(&outcome);
        assert(result);
        co_return std::move(*result);
    }

    if (!(dynamic_cast<RemoteStore *>(&*builderStore) || dynamic_cast<LegacySSHStore *>(&*builderStore)))
        throw Error(
            "cannot use '%s' as a remote builder: only local stores, 'ssh://' and 'ssh-ng://' are supported", storeUri);

    /* Build over the store interface. */

    std::unique_ptr<LogFile> logFile = std::make_unique<LogFile>(worker.store, drvPath, settings.getLogFileSettings());

    buildResult.startTime = time(nullptr); // inexact

    auto msg =
        fmt(buildMode == bmRepair  ? "repairing outputs of '%s'"
            : buildMode == bmCheck ? "checking outputs of '%s'"
                                   : "building '%s'",
            worker.store.printStorePath(drvPath));
    msg += fmt(" on '%s'", storeUri);

    std::unique_ptr<BuildLog> buildLog = std::make_unique<BuildLog>(
        worker.settings.logLines,
        std::make_unique<Activity>(
            *logger,
            lvlInfo,
            actBuild,
            msg,
            std::to_array<Logger::Field>({worker.store.printStorePath(drvPath), storeUri, 1, 1})));
    mcRunningBuilds = std::make_unique<MaintainCount<uint64_t>>(worker.runningBuilds);
    worker.updateProgress();

    SingleDrvOutputs remoteOutputs;

    /* The actual build, blocking on the RPC, so on the thread pool. This
       mirrors what `nix __build-remote` did. */
    auto build = [&]() -> std::optional<BuildError> {
        std::optional<BuildResult> optResult;

        // If we don't know whether we are trusted (e.g. `ssh://`
        // stores), we assume we are. This is necessary for backwards
        // compat.
        bool trustedOrLegacy = ({
            std::optional trusted = builderStore->isTrustedClient();
            !trusted || *trusted;
        });

        // See the very large comment in `case WorkerProto::Op::BuildDerivation:` in
        // `src/libstore/daemon.cc` that explains the trust model here.
        //
        // This condition mirrors that: that code enforces the "rules" outlined there;
        // we do the best we can given those "rules".
        if (trustedOrLegacy || derivation::type(*drv).isCA()) {
            /* `drv` already has the outputs of the input derivations
               merged into its inputs, which is what the remote side needs
               (see `DerivationGoal`). */
            if (auto * remoteStore = dynamic_cast<RemoteStore *>(&*builderStore))
                /* Keep our own copy of the build log (for `nix log`); the
                   goal is suspended waiting on this thread, so the log
                   objects are ours alone to write to. */
                optResult = remoteStore->buildDerivationWithLog(drvPath, *drv, bmNormal, [&](std::string_view line) {
                    auto data = std::string(line) + "\n";
                    (*buildLog)(data);
                    if (logFile->sink)
                        (*logFile->sink)(data);
                });
            else
                optResult = builderStore->getBuilder()->buildDerivation(drvPath, *drv);
        } else {
            copyClosure(worker.store, *builderStore, StorePathSet{drvPath}, NoRepair, NoCheckSigs, substitute);
            auto res = builderStore->getBuilder()->buildPathsWithResults({DerivedPath::Built{
                .drvPath = makeConstantStorePathRef(drvPath),
                .outputs = OutputsSpec::All{},
            }});
            // One path to build should produce exactly one build result
            assert(res.size() == 1);
            optResult = std::move(res[0]);
        }

        auto & result = *optResult;
        if (auto * failureP = result.tryGetFailure()) {
            if (worker.settings.keepFailed)
                warn(
                    "The failed build directory was kept on the remote builder due to `--keep-failed`. "
                    "If it can be built locally, re-run the command with `--builders ''` to disable remote "
                    "building for this invocation.");
            return BuildError(
                BuildResult::Failure::MiscFailure,
                "build of '%s' on '%s' failed: %s",
                worker.store.printStorePath(drvPath),
                storeUri,
                failureP->message());
        }

        /* Outputs of the remote build, to copy back. Older remotes may
           not report them, in which case they have to be known a priori. */
        auto & success = *result.tryGetSuccess();
        if (success.builtOutputs.empty())
            for (auto & [outputName, hopefullyOutputPath] : outputsAndOptPaths(*drv, worker.store)) {
                assert(hopefullyOutputPath.second);
                success.builtOutputs.insert_or_assign(
                    outputName, UnkeyedRealisation{.outPath = *hopefullyOutputPath.second});
            }
        remoteOutputs = std::move(success.builtOutputs);
        return std::nullopt;
    };

#ifndef _WIN32
    /* Meanwhile, pass the build log along. */
    auto readLog = [&]() -> asio::awaitable<void> {
        if (logPipe.readSide) {
            ChildEvents events(co_await asio::this_coro::executor, {logPipe.readSide.get()}, std::nullopt);
            while (true) {
                auto event = co_await events.next();
                if (auto * output = std::get_if<ChildOutput>(&event)) {
                    (*buildLog)(output->data);
                    if (logFile->sink)
                        (*logFile->sink)(output->data);
                } else if (std::get_if<ChildEOF>(&event))
                    break;
            }
        }
        /* Nothing more to read, but the build is not done: keep waiting
           so that the build's completion is what ends the race below. */
        asio::steady_timer forever(co_await asio::this_coro::executor);
        forever.expires_at(std::chrono::steady_clock::time_point::max());
        co_await forever.async_wait(asio::use_awaitable);
    };
#endif

    std::optional<BuildError> failure;
    {
        using namespace asio::experimental::awaitable_operators;
#ifndef _WIN32
        auto res = co_await (onThreadPool(pool, build) || readLog());
        failure = std::move(std::get<0>(res));
#else
        failure = co_await onThreadPool(pool, build);
#endif
    }

    trace("remote build done");

    buildLog->flush();

    buildResult.timesBuilt++;
    buildResult.stopTime = time(nullptr);

    /* Close the log file. */
    logFile.reset();

    if (failure) {
        outputLocks.unlock();
        /* TODO (once again) support fine-grained error codes, see issue #12641. */
        co_return std::move(*failure);
    }

    co_await copyOutputsFromBuilder(*builderStore, storeUri, remoteOutputs);

    /* Aborts if any output is not valid or corrupt, and otherwise
       returns a 'SingleDrvOutputs' structure containing all outputs. */
    auto builtOutputs = [&] {
        auto [allValid, validOutputs] = checkPathValidity(initialOutputs);
        if (!allValid)
            throw Error("some outputs are unexpectedly invalid");
        return validOutputs;
    }();

    StorePathSet outputPaths;
    for (auto & [_, output] : builtOutputs)
        outputPaths.insert(output.outPath);

    if (worker.settings.postBuildHook.get() != "") {
#ifdef _WIN32
        throw UnimplementedError("the post-build hook is not yet supported on Windows");
#else
        co_await runPostBuildHook(worker.settings, worker.store, *logger, drvPath, outputPaths);
#endif
    }

    /* It is now safe to delete the lock files, since all future
       lockers will see that the output paths are valid; they will
       not create new lock files with the same names as the old
       (unlinked) lock files. */
    outputLocks.setDeletion(true);
    outputLocks.unlock();

    co_return BuildResult::Success{
        .status = BuildResult::Success::Built,
        .builtOutputs = std::move(builtOutputs),
    };
}

asio::awaitable<DerivationBuildingGoal::LocalBuildOutcome> DerivationBuildingGoal::buildLocally(
    LocalBuildCapability localBuildCap,
    const StorePathSet & inputPaths,
    std::map<std::string, InitialOutput> & initialOutputs,
    const DerivationOptions<StorePath> & drvOptions,
    PathLocks outputLocks,
    std::optional<AsyncSemaphore::Handle> * buildSlot,
    std::optional<std::string> builderName)
{
    std::unique_ptr<BuildLog> buildLog;
    std::unique_ptr<LogFile> logFile;

    auto openLogFile = [&]() {
        logFile = std::make_unique<LogFile>(worker.store, drvPath, settings.getLogFileSettings());
    };

    auto closeLogFile = [&]() { logFile.reset(); };

    auto started = [&]() {
        auto msg =
            fmt(buildMode == bmRepair  ? "repairing outputs of '%s'"
                : buildMode == bmCheck ? "checking outputs of '%s'"
                                       : "building '%s'",
                worker.store.printStorePath(drvPath));
        if (builderName)
            msg += fmt(" on '%s'", *builderName);
        buildLog = std::make_unique<BuildLog>(
            worker.settings.logLines,
            std::make_unique<Activity>(
                *logger,
                lvlInfo,
                actBuild,
                msg,
                std::to_array<Logger::Field>({worker.store.printStorePath(drvPath), builderName.value_or(""), 1, 1})));
        mcRunningBuilds = std::make_unique<MaintainCount<uint64_t>>(worker.runningBuilds);
        worker.updateProgress();
    };

    std::unique_ptr<Activity> actLock;
    DerivationBuilderUnique builder;

    // Will continue here while waiting for a build user below
    while (true) {

        /* Take a local build slot, unless the caller says we don't need
           one (building in another store, limited by its own slots). */
        if (buildSlot && !*buildSlot)
            *buildSlot = worker.buildSemaphore.tryAcquire();
        if (buildSlot && !*buildSlot) {
            if (worker.settings.maxBuildJobs == 0U) {
                if (worker.machines().empty())
                    throw Error(
                        "Unable to start any build; either increase '--max-jobs' or enable remote builds.\n"
                        "\n"
                        "For more information run 'man nix.conf' and search for '/machines'.");
                else
                    throw Error(
                        "Unable to start any build; remote machines may not have all required system features.\n"
                        "\n"
                        "For more information run 'man nix.conf' and search for '/machines'.");
            }
            outputLocks.unlock();
            /* Wait for a slot to open up, then start over so that a build
               hook gets another chance before we build locally. We keep
               the slot while retrying. */
            *buildSlot = co_await worker.buildSemaphore.asyncAcquire();
            co_return NeedsSlot{};
        }

        if (!builder) {
            /**
             * Local implementation of these virtual methods, consider
             * this just a record of lambdas.
             */
            struct DerivationBuildingGoalCallbacks : DerivationBuilderCallbacks
            {
                DerivationBuildingGoal & goal;
                fun<void()> openLogFileFn;
                fun<void()> closeLogFileFn;

                DerivationBuildingGoalCallbacks(
                    DerivationBuildingGoal & goal, fun<void()> openLogFileFn, fun<void()> closeLogFileFn)
                    : goal{goal}
                    , openLogFileFn{std::move(openLogFileFn)}
                    , closeLogFileFn{std::move(closeLogFileFn)}
                {
                }

                ~DerivationBuildingGoalCallbacks() override = default;

                void openLogFile() override
                {
                    openLogFileFn();
                }

                void closeLogFile() override
                {
                    closeLogFileFn();
                }

                void processDaemonConnection(
                    ref<Store> store,
                    FdSource && from,
                    FdSink && to,
                    RestrictionContext & context,
                    daemon::RecursiveFlag recursiveFlag) override
                {
                    /* The daemon thread is just another caller of the
                       (thread-safe) worker, whose goal for this build is
                       suspended waiting on the builder, so the recursive
                       builds share its scheduling state. This build holds
                       a build slot while it waits, so lend it to them, or
                       with `max-jobs = 1` they could never start. */
                    goal.worker.lendBuildSlot();
                    Finally reclaim([this] { goal.worker.reclaimBuildSlot(); });
                    auto builder = makeRestrictedBuilder(goal.worker, context);
                    daemon::processConnection(
                        store, std::move(from), std::move(to), NotTrusted, recursiveFlag, builder.get_ptr());
                }
            };

            decltype(DerivationBuilderParams::defaultPathsInChroot) defaultPathsInChroot =
                localBuildCap.localStore.config->getLocalSettings().sandboxPaths.get();
            DesugaredEnv desugaredEnv;

            /* Add the closure of store paths to the chroot. */
            StorePathSet closure;
            for (auto & i : defaultPathsInChroot)
                try {
                    if (worker.store.isInStore(i.second.source.string()))
                        worker.store.computeFSClosure(
                            worker.store.toStorePath(i.second.source.string()).first, closure);
                } catch (InvalidPath & e) {
                } catch (Error & e) {
                    e.addTrace({}, "while processing sandbox path %s", PathFmt(i.second.source));
                    throw;
                }
            for (auto & i : closure) {
                auto p = worker.store.printStorePath(i);
                defaultPathsInChroot.insert_or_assign(p, ChrootPath{.source = p});
            }

            try {
                desugaredEnv = DesugaredEnv::create(worker.store, *drv, drvOptions, inputPaths);
            } catch (BuildError & e) {
                outputLocks.unlock();
                co_return std::move(e);
            }

            DerivationBuilderParams params{
                .drvPath = drvPath,
                .buildResult = buildResult,
                .drv = *drv,
                .drvOptions = drvOptions,
                .inputPaths = inputPaths,
                .initialOutputs = initialOutputs,
                .buildMode = buildMode,
                .defaultPathsInChroot = std::move(defaultPathsInChroot),
                .systemFeatures = worker.store.config.systemFeatures.get(),
                .desugaredEnv = std::move(desugaredEnv),
            };

            /* If we have to wait and retry (see below), then `builder` will
               already be created, so we don't need to create it again. */
            builder = localBuildCap.externalBuilder
                          ?
#ifdef _WIN32
                          /* No external-builder support on Windows yet. */
                          throw UnimplementedError("external builders are not yet supported on Windows")
#else
                          makeExternalDerivationBuilder(
                              makeBuildingStoreFromLocalStore(localBuildCap.localStore),
                              std::make_shared<DerivationBuildingGoalCallbacks>(*this, openLogFile, closeLogFile),
                              std::move(params),
                              *localBuildCap.externalBuilder)
#endif
                          : makeDerivationBuilder(
                                makeBuildingStoreFromLocalStore(localBuildCap.localStore),
                                std::make_shared<DerivationBuildingGoalCallbacks>(*this, openLogFile, closeLogFile),
                                std::move(params));
        }

        if (!builder->startBuild()) {
            if (!actLock)
                actLock = std::make_unique<Activity>(
                    *logger,
                    lvlWarn,
                    actBuildWaiting,
                    fmt("waiting for a free build user ID for '%s'", Magenta(worker.store.printStorePath(drvPath))));
            co_await waitForAWhile();
            continue;
        }

        break;
    }

    actLock.reset();

    ChildEvents events(
        co_await asio::this_coro::executor,
        {builder->logDescriptor()},
        ChildTimeouts{
            .maxSilentTime = worker.settings.maxSilentTime,
            .buildTimeout = worker.settings.buildTimeout,
        });

    started();

    uint64_t logSize = 0;

    while (true) {
        auto event = co_await events.next();
        if (auto * output = std::get_if<ChildOutput>(&event)) {
            if (output->fd == builder->logDescriptor()) {
                logSize += output->data.size();
                if (worker.settings.maxLogSize && logSize > worker.settings.maxLogSize) {
                    builder->killChild();
                    co_return logLimitExceeded();
                }
                (*buildLog)(output->data);
                if (logFile->sink)
                    (*logFile->sink)(output->data);
            }
        } else if (std::get_if<ChildEOF>(&event)) {
            buildLog->flush();
            break;
        } else if (auto * timeout = std::get_if<TimedOut>(&event)) {
            builder->killChild();
            co_return std::move(*timeout);
        }
    }

    trace("build done");

    auto [status, diskFull] = builder->unprepareBuild();

    /* Check the exit status. */
    if (!statusOk(status)) {
        builder->cleanupBuild(false);
        builder.reset();
        outputLocks.unlock();
        co_return fixupBuilderFailureErrorMessage(
            {
                !derivation::type(*drv).isSandboxed() || diskFull ? BuildResult::Failure::TransientFailure
                                                                  : BuildResult::Failure::PermanentFailure,
                status,
                diskFull ? "\nnote: build failure may have been caused by lack of free disk space" : "",
            },
            *buildLog);
    }

    SingleDrvOutputs builtOutputs;
    try {
        /* Compute the FS closure of the outputs and register them as
           being valid. With builder-rpc-v0 the builder already submitted
           the outputs, so check those instead. */
        builtOutputs = drvOptions.getRequiredSystemFeatures(*drv).count(std::string{drvFeatureBuilderRpcV0})
                           ? builder->checkSubmittedOutputs(localBuildCap.localStore)
                           : builder->registerOutputs(localBuildCap.localStore);
        builder->cleanupBuild(true);
    } catch (BuilderFailureError & e) {
        builder.reset();
        outputLocks.unlock();
        co_return fixupBuilderFailureErrorMessage(std::move(e), *buildLog);
    } catch (BuildError & e) {
        builder.reset();
        outputLocks.unlock();
        co_return std::move(e);
    }

    /* When building in a local store other than our own (a builder
       from `builders`), the outputs are valid there now, but not yet
       here. The post-build hook runs there too, as it would have on any
       other builder, before it runs here for the copied outputs. */
    if (&localBuildCap.localStore != &worker.store) {
        if (worker.settings.postBuildHook.get() != "") {
            StorePathSet outputPaths;
            for (auto & [_, output] : builtOutputs)
                outputPaths.insert(output.outPath);
#ifdef _WIN32
            throw UnimplementedError("the post-build hook is not yet supported on Windows");
#else
            co_await runPostBuildHook(worker.settings, localBuildCap.localStore, *logger, drvPath, outputPaths);
#endif
        }
        co_await copyOutputsFromBuilder(localBuildCap.localStore, builderName.value_or("?"), builtOutputs);
    }

    {
        builder.reset();
        StorePathSet outputPaths;
        /* In the check case we install no store objects, and so
           `builtOutputs` is empty. However, per issue #14287, there is
           an expectation that the post-build hook is still executed.
           (This is useful for e.g. logging successful deterministic rebuilds.)

           In order to make that work, in the check case just load the
           (preexisting) infos from scratch, rather than relying on what
           `DerivationBuilder` returned to us. */
        for (auto & [_, output] : buildMode == bmCheck ? checkPathValidity(initialOutputs).second : builtOutputs) {
            // for sake of `bmRepair`
            worker.markContentsGood(output.outPath);
            outputPaths.insert(output.outPath);
        }

        if (worker.settings.postBuildHook.get() != "") {
#ifdef _WIN32
            /* Nothing here needs `fork`: the child only sets the environment,
               redirects stdout/stderr and execs, which `spawnProcess` already
               does. What is missing is that `spawnProcess` is not exported.
               Throw rather than silently skip the hook. */
            throw UnimplementedError("the post-build hook is not yet supported on Windows");
#else
            co_await runPostBuildHook(worker.settings, worker.store, *logger, drvPath, outputPaths);
#endif
        }

        /* It is now safe to delete the lock files, since all future
           lockers will see that the output paths are valid; they will
           not create new lock files with the same names as the old
           (unlinked) lock files. */
        outputLocks.setDeletion(true);
        outputLocks.unlock();
        co_return BuildResult::Success{
            .status = BuildResult::Success::Built,
            .builtOutputs = std::move(builtOutputs),
        };
    }
}

#ifndef _WIN32
static asio::awaitable<void> runPostBuildHook(
    const WorkerSettings & workerSettings,
    const StoreDirConfig & store,
    Logger & logger,
    const StorePath & drvPath,
    const StorePathSet & outputPaths)
{
    auto state =
        std::make_unique<PostBuildHookState>(logger, workerSettings.postBuildHook.get(), store.printStorePath(drvPath));

    auto hook = workerSettings.postBuildHook.get();

    OsStringMap hookEnvironment = getEnvOs();

    hookEnvironment.emplace(OS_STR("DRV_PATH"), string_to_os_string(store.printStorePath(drvPath)));
    hookEnvironment.emplace(
        OS_STR("OUT_PATHS"), string_to_os_string(chomp(concatStringsSep(" ", store.printStorePathSet(outputPaths)))));
    hookEnvironment.emplace(OS_STR("NIX_CONFIG"), string_to_os_string(globalConfig.toKeyValue()));

    ProcessOptions processOptions;

    state->pid = startProcess(
        [&] {
            replaceEnv(hookEnvironment);
            if (dup2(state->out->writeSide.get(), STDOUT_FILENO) == -1)
                throw SysError("dupping stdout");
            if (dup2(STDOUT_FILENO, STDERR_FILENO) == -1)
                throw SysError("cannot dup stdout into stderr");

            Strings args_;
            args_.push_front(hook);

            restoreProcessContext();

            /* On Linux, it's crucial that this is done after restoreProcessContext() since
               that needs an open mountns file descriptor (fdSavedMountNamespace). */
            unix::closeExtraFDs();

            execvp(requireCString(hook), stringsToCharPtrs(args_).data());

            throw SysError("executing %s", PathFmt(hook));
        },
        processOptions);

    state->out->writeSide.close();

    ChildEvents events(co_await asio::this_coro::executor, {state->out->readSide.get()}, std::nullopt);
    while (true) {
        auto event = co_await events.next();
        if (auto * output = std::get_if<ChildOutput>(&event)) {
            (*state->sink)(output->data);
        } else if (std::get_if<ChildEOF>(&event)) {
            state->complete();
            break;
        }
    }
}
#endif

BuildError DerivationBuildingGoal::fixupBuilderFailureErrorMessage(BuilderFailureError e, BuildLog & buildLog)
{
    auto msg =
        fmt("Cannot build '%s'.\n"
            "Reason: " ANSI_RED "builder %s" ANSI_NORMAL ".",
            Magenta(worker.store.printStorePath(drvPath)),
            statusToString(e.builderStatus));

    msg += showKnownOutputs(worker.store, *drv);

    auto & logTail = buildLog.getTail();
    if (!logger->isVerbose() && !logTail.empty()) {
        msg += fmt("\nLast %d log lines:\n", logTail.size());
        for (auto & line : logTail) {
            msg += "> ";
            msg += line;
            msg += "\n";
        }
        auto nixLogCommand = experimentalFeatureSettings.isEnabled(Xp::NixCommand) ? "nix log" : "nix-store -l";
        // The command is on a separate line for easy copying, such as with triple click.
        // This message will be indented elsewhere, so removing the indentation before the
        // command will not put it at the start of the line unfortunately.
        msg +=
            fmt("For full logs, run:\n  " ANSI_BOLD "%s %s" ANSI_NORMAL,
                nixLogCommand,
                worker.store.printStorePath(drvPath));
    }

    msg += e.extraMsgAfter;

    return BuildError{e.status, msg};
}

LogFile::LogFile(Store & store, const StorePath & drvPath, const LogFileSettings & logSettings)
{
    if (!logSettings.keepLog)
        return;

    auto baseName = std::string(baseNameOf(store.printStorePath(drvPath)));

    auto dir = store.config.getLogDir() / LocalFSStore::drvsLogDir / baseName.substr(0, 2);
    createDirs(dir);

    auto logFileName = dir / (baseName.substr(2) + (logSettings.compressLog ? ".bz2" : ""));

    fd = openNewFileForWrite(
        logFileName,
        0666,
        {
            .truncateExisting = true,
            .followSymlinksOnTruncate = true, /* FIXME: Probably shouldn't follow symlinks. */
        });
    if (!fd)
        throw SysError("creating log file %1%", PathFmt(logFileName));

    fileSink = std::make_shared<FdSink>(fd.get());

    if (logSettings.compressLog)
        sink = std::shared_ptr<CompressionSink>(makeCompressionSink(CompressionAlgo::bzip2, *fileSink));
    else
        sink = fileSink;
}

LogFile::~LogFile()
{
    try {
        auto sink2 = std::dynamic_pointer_cast<CompressionSink>(sink);
        if (sink2)
            sink2->finish();
        if (fileSink)
            fileSink->flush();
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

BuildError DerivationBuildingGoal::logLimitExceeded()
{
    return BuildError(
        BuildResult::Failure::LogLimitExceeded,
        "%s killed after writing more than %d bytes of log output",
        getName(),
        worker.settings.maxLogSize);
}

std::map<std::string, std::optional<StorePath>> DerivationBuildingGoal::queryPartialDerivationOutputMap()
{
    assert(!type(*drv).isImpure());

    for (auto * drvStore : {&worker.evalStore, &worker.store})
        if (drvStore->isValidPath(drvPath))
            return worker.store.queryPartialDerivationOutputMap(drvPath, drvStore);

    /* In-memory derivation will naturally fall back on this case, where
       we do best-effort with static information. */
    std::map<std::string, std::optional<StorePath>> res;
    for (auto & [name, output] : drv->outputs)
        res.insert_or_assign(name, output.path(worker.store, drv->name, name));
    return res;
}

std::pair<bool, SingleDrvOutputs>
DerivationBuildingGoal::checkPathValidity(std::map<std::string, InitialOutput> & initialOutputs)
{
    if (type(*drv).isImpure())
        return {false, {}};

    bool checkHash = buildMode == bmRepair;
    SingleDrvOutputs validOutputs;

    for (auto & i : queryPartialDerivationOutputMap()) {
        auto initialOutput = get(initialOutputs, i.first);
        if (!initialOutput)
            // this is an invalid output, gets caught with (!wantedOutputsLeft.empty())
            continue;
        auto & info = *initialOutput;
        if (i.second) {
            auto outputPath = *i.second;
            info.known = {
                .path = outputPath,
                .status = !worker.store.isValidPath(outputPath)               ? PathStatus::Absent
                          : !checkHash || worker.pathContentsGood(outputPath) ? PathStatus::Valid
                                                                              : PathStatus::Corrupt,
            };
        }
        auto drvOutput = DrvOutput{drvPath, i.first};
        if (experimentalFeatureSettings.isEnabled(Xp::CaDerivations)) {
            if (auto real = worker.store.queryRealisation(drvOutput)) {
                info.known = {
                    .path = real->outPath,
                    .status = worker.store.isValidPath(real->outPath) ? PathStatus::Valid : PathStatus::Absent,
                };
            } else if (info.known && info.known->isValid()) {
                // We know the output because it's a static output of the
                // derivation, and the output path is valid, but we don't have
                // its realisation stored (probably because it has been built
                // without the `ca-derivations` experimental flag).
                worker.store.registerDrvOutput(
                    Realisation{
                        {
                            .outPath = info.known->path,
                        },
                        drvOutput,
                    },
                    NoCheckSigs);
            }
        }
        if (info.known && info.known->isValid())
            validOutputs.emplace(
                i.first,
                Realisation{
                    {
                        .outPath = info.known->path,
                    },
                    drvOutput,
                });
    }

    bool allValid = true;
    for (auto & [_, status] : initialOutputs) {
        if (!status.known || !status.known->isValid()) {
            allValid = false;
            break;
        }
    }

    return {allValid, validOutputs};
}

} // namespace nix
