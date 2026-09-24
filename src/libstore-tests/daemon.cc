#if !defined(_WIN32) && !defined(__CYGWIN__)

#  include <gtest/gtest.h>

#  include "nix/store/daemon.hh"
#  include "nix/store/dummy-store.hh"
#  include "nix/util/file-descriptor.hh"
#  include "nix/util/finally.hh"
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

} // namespace nix

#endif
