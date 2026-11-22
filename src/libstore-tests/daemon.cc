#if !defined(_WIN32) && !defined(__CYGWIN__)

#  include <gtest/gtest.h>
#  include <csignal>
#  include <thread>

#  include "nix/store/daemon.hh"
#  include "nix/store/dummy-store.hh"
#  include "nix/store/globals.hh"
#  include "nix/store/worker-protocol-connection.hh"
#  include "nix/util/file-descriptor.hh"
#  include "nix/util/finally.hh"
#  include "nix/util/logging.hh"
#  include "nix/util/signals.hh"

namespace nix {

TEST(DaemonConnection, interruptedHandshakeClearsInterrupt)
{
    auto config = make_ref<DummyStoreConfig>(StoreReference::Params{});
    auto store = config->openStore();

    Pipe fromClient, toClient;
    fromClient.create();
    toClient.create();
    FdSink clientSink(fromClient.writeSide.get());
    clientSink << 0; // Keep the read ready. The interrupt is detected before this value is checked.
    clientSink.flush();

    // Interrupt the first handshake read, before request processing begins.
    setInterrupted(true);
    Finally clearInterruptFlag([] { setInterrupted(false); });

    EXPECT_THROW(
        daemon::processConnection(
            store,
            FdSource(fromClient.readSide.get()),
            FdSink(toClient.writeSide.get()),
            Trusted,
            daemon::RecursiveFlag::NotRecursive),
        Interrupted);
    EXPECT_FALSE(getInterrupted());
}

namespace {

struct PipeClientConnection : WorkerProto::BasicClientConnection
{
    void closeWrite() override {}
};

/**
 * Connects to a daemon worker as a client with protocol `features`,
 * sends `SetOptions` with `verboseBuild`, and returns the worker
 * logger's `isVerbose()`.
 */
bool workerIsVerboseAfterSetOptions(const WorkerProto::Version::FeatureSet & features, bool verboseBuild)
{
    auto store = make_ref<DummyStoreConfig>(StoreReference::Params{})->openStore();

    Pipe fromClient, toClient;
    fromClient.create();
    toClient.create();

    /* When the client hangs up, the worker interrupts its thread with
       NIX_SIG_MULTI_INT, which needs a handler, as in `initNix`. */
    struct sigaction act;
    act.sa_handler = [](int) {};
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    if (sigaction(NIX_SIG_MULTI_INT, &act, nullptr))
        throw SysError("installing handler for NIX_SIG_MULTI_INT");

    std::exception_ptr workerException;
    std::thread worker([&] {
        /* Lets the client see the end of the connection, also when the
           worker fails before it replies. */
        Finally closeToClient([&] { toClient.writeSide.close(); });
        try {
            daemon::processConnection(
                store,
                FdSource(fromClient.readSide.get()),
                FdSink(toClient.writeSide.get()),
                Trusted,
                daemon::RecursiveFlag::NotRecursive);
        } catch (...) {
            workerException = std::current_exception();
        }
    });

    bool verbose = false;
    std::exception_ptr clientException;
    {
        Finally stopWorker([&] {
            fromClient.writeSide.close();
            worker.join();
        });

        try {
            PipeClientConnection conn;
            conn.to = FdSink(fromClient.writeSide.get());
            conn.from = FdSource(toClient.readSide.get());
            auto localVersion = WorkerProto::latest;
            localVersion.features = features;
            conn.protoVersion = WorkerProto::BasicClientConnection::handshake(conn.to, conn.from, localVersion);
            conn.postHandshake(*store);
            if (auto ex = conn.processStderrReturn())
                std::rethrow_exception(ex);

            /* The current settings, except `verboseBuild`. */
            conn.to << WorkerProto::Op::SetOptions << settings.keepFailed << settings.getWorkerSettings().keepGoing
                    << settings.getWorkerSettings().tryFallback << std::to_underlying(verbosity)
                    << settings.getWorkerSettings().maxBuildJobs << settings.getWorkerSettings().maxSilentTime
                    << true                                                        // obsolete useBuildHook
                    << std::to_underlying(verboseBuild ? lvlError : lvlVomit) << 0 // obsolete log type
                    << 0                                                           // obsolete print build trace
                    << settings.getLocalSettings().buildCores << settings.getWorkerSettings().useSubstitutes
                    << 0; // no overrides
            if (auto ex = conn.processStderrReturn())
                std::rethrow_exception(ex);

            /* The worker now waits for the next operation. */
            verbose = logger->isVerbose();
        } catch (...) {
            clientException = std::current_exception();
        }
    }

    /* A worker failure explains the client's. */
    if (workerException)
        std::rethrow_exception(workerException);
    if (clientException)
        std::rethrow_exception(clientException);
    return verbose;
}

/**
 * Expects `workerIsVerboseAfterSetOptions(features, verboseBuild)` to
 * return `expected`, running it in a child process. The worker replaces
 * the global logger, applies the client's settings, and installs a
 * signal handler, so none of that may reach other tests.
 *
 * The child reports on stderr, because gtest does not show assertion
 * failures from a death-test child.
 */
void expectWorkerIsVerbose(const WorkerProto::Version::FeatureSet & features, bool verboseBuild, bool expected)
{
    EXPECT_EXIT(
        {
            try {
                bool verbose = workerIsVerboseAfterSetOptions(features, verboseBuild);
                dprintf(STDERR_FILENO, "worker isVerbose: %s\n", verbose ? "true" : "false");
                _exit(0);
            } catch (const std::exception & e) {
                dprintf(STDERR_FILENO, "%s\n", e.what());
                _exit(1);
            }
        },
        ::testing::ExitedWithCode(0),
        expected ? "worker isVerbose: true" : "worker isVerbose: false");
}

} // namespace

TEST(DaemonConnection, workerVerboseWhenClientWithFeatureShowsLogs)
{
    auto features = WorkerProto::latest.features;
    ASSERT_TRUE(features.contains(WorkerProto::featureClientSendsVerboseBuildFlag));
    expectWorkerIsVerbose(features, true, true);
}

TEST(DaemonConnection, workerNotVerboseWhenClientWithFeatureHidesLogs)
{
    auto features = WorkerProto::latest.features;
    ASSERT_TRUE(features.contains(WorkerProto::featureClientSendsVerboseBuildFlag));
    expectWorkerIsVerbose(features, false, false);
}

TEST(DaemonConnection, verboseBuildIgnoredFromOlderClient)
{
    auto features = WorkerProto::latest.features;
    features.erase(std::string{WorkerProto::featureClientSendsVerboseBuildFlag});
    expectWorkerIsVerbose(features, true, false);
}

} // namespace nix

#endif
