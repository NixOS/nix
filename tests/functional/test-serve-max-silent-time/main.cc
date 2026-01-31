#include <cstdlib>
#include <iostream>
#include <string>

#include "nix/store/derivations.hh"
#include "nix/store/globals.hh"
#include "nix/store/serve-protocol-connection.hh"
#include "nix/store/serve-protocol.hh"
#include "nix/store/store-open.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/processes.hh"

using namespace nix;

static std::string hostLabel()
{
    const char * value = std::getenv("NIX_REMOTE");
    if (value && *value)
        return std::string("nix-store --serve (NIX_REMOTE=") + value + ")";
    return "nix-store --serve";
}

int main(int argc, char ** argv)
{
    try {
        if (argc != 7) {
            std::cerr
                << "Usage: " << argv[0]
                << " drv1 maxSilent1 expect-timeout|expect-success drv2 maxSilent2 expect-timeout|expect-success\n";
            return 1;
        }

        std::string drvPathStr1 = argv[1];
        unsigned int maxSilentTime1 = std::stoul(argv[2]);
        std::string expect1 = argv[3];
        std::string drvPathStr2 = argv[4];
        unsigned int maxSilentTime2 = std::stoul(argv[5]);
        std::string expect2 = argv[6];

        initLibStore();
        auto store = openStore();

        Pipe toChild;
        Pipe fromChild;
        toChild.create();
        fromChild.create();

        Pid child(startProcess([&]() {
            if (dup2(toChild.readSide.get(), STDIN_FILENO) == -1)
                throw SysError("dup2 stdin");
            if (dup2(fromChild.writeSide.get(), STDOUT_FILENO) == -1)
                throw SysError("dup2 stdout");
            unix::closeExtraFDs();
            execlp("nix-store", "nix-store", "--serve", "--write", (char *) nullptr);
            throw SysError("exec nix-store");
        }));
        child.setKillSignal(SIGTERM);

        toChild.readSide.close();
        fromChild.writeSide.close();

        ServeProto::BasicClientConnection conn;
        conn.to = FdSink(toChild.writeSide.get());
        conn.from = FdSource(fromChild.readSide.get());
        conn.remoteVersion =
            ServeProto::BasicClientConnection::handshake(conn.to, conn.from, SERVE_PROTOCOL_VERSION, hostLabel());

        auto runBuild = [&](const std::string & drvPathStr, unsigned int maxSilentTime, const std::string & expect) {
            bool expectTimeout = expect == "expect-timeout";
            bool expectSuccess = expect == "expect-success";
            if (!expectTimeout && !expectSuccess)
                throw Error("invalid expectation: %s", expect);

            auto drvPath = store->parseStorePath(drvPathStr);
            auto drv = store->readDerivation(drvPath);
            auto basicDrvOpt = drv.tryResolve(*store, &*store);
            if (!basicDrvOpt)
                throw Error("could not resolve derivation inputs for '%s'", drvPathStr);

            ServeProto::BuildOptions options;
            options.maxSilentTime = maxSilentTime;
            options.buildTimeout = 0;
            options.maxLogSize = 0;
            options.nrRepeats = 0;
            options.enforceDeterminism = false;
            options.keepFailed = false;

            conn.putBuildDerivationRequest(*store, drvPath, *basicDrvOpt, options);
            auto result = conn.getBuildDerivationResponse(*store);

            if (auto * failure = result.tryGetFailure()) {
                if (failure->status == BuildResult::Failure::TimedOut) {
                    if (expectTimeout)
                        return;
                    throw Error("build unexpectedly timed out");
                }
                throw Error("unexpected failure: %s", failure->msg());
            }

            if (expectSuccess)
                return;
            throw Error("build unexpectedly succeeded");
        };

        runBuild(drvPathStr1, maxSilentTime1, expect1);
        runBuild(drvPathStr2, maxSilentTime2, expect2);

        toChild.writeSide.close();
        fromChild.readSide.close();
        child.kill();

        return 0;

    } catch (const std::exception & e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
