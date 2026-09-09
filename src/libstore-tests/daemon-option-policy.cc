#include <gtest/gtest.h>
#include <thread>

#ifndef _WIN32
#  include <sys/socket.h>
#endif

#include "nix/store/daemon.hh"
#include "nix/store/daemon-option-policy.hh"
#include "nix/store/dummy-store-impl.hh"
#include "nix/store/globals.hh"
#include "nix/store/filetransfer.hh"
#include "nix/store/uds-remote-store.hh"
#include "nix/store/remote-store-connection.hh"

namespace nix {

TEST(DaemonOptionPolicy, clientOnlySettingsAreNeverForwarded)
{
    Settings settings;
    FileTransferSettings transfers;
    settings.set("use-xdg-base-directories", "true");
    settings.set("store", "daemon");
    settings.set("print-missing", "false");
    auto options = getForwardedDaemonOptions(settings, transfers, std::nullopt);
    EXPECT_TRUE(options.overrides.empty());
    EXPECT_TRUE(options.rejected.empty());
}

TEST(DaemonOptionPolicy, fixedFieldsAreNotDuplicated)
{
    Settings settings;
    FileTransferSettings transfers;
    settings.set("keep-going", "true");
    settings.set("max-jobs", "4");
    settings.set("max-silent-time", "10");
    settings.set("cores", "2");
    auto policy = getDaemonOptionPolicy(settings, transfers, true);
    EXPECT_EQ(classifyDaemonOption("keep-going"), DaemonOptionKind::Fixed);
    EXPECT_FALSE(policy.contains("keep-going"));
    EXPECT_FALSE(policy.contains("max-jobs"));
    EXPECT_FALSE(policy.contains("max-silent-time"));
    EXPECT_FALSE(policy.contains("cores"));
    EXPECT_TRUE(getForwardedDaemonOptions(settings, transfers, std::nullopt).overrides.empty());
}

TEST(DaemonOptionPolicy, untrustedRestrictions)
{
    Settings settings;
    FileTransferSettings transfers;
    auto policy = getDaemonOptionPolicy(settings, transfers, false);
    EXPECT_EQ(policy.at("timeout"), daemonOptionRuleAny);
    EXPECT_EQ(policy.at("connect-timeout"), daemonOptionRuleAny);
    EXPECT_FALSE(policy.contains("sandbox"));
    EXPECT_FALSE(policy.contains("trusted-public-keys"));
    EXPECT_TRUE(daemonOptionAllowsValue(policy.at("builders"), ""));
    EXPECT_FALSE(daemonOptionAllowsValue(policy.at("builders"), "ssh-ng://builder"));
    EXPECT_EQ(policy.at("substituters"), daemonOptionRuleSubstituters);
    EXPECT_FALSE(daemonOptionAllowsValue("unknown-future-rule", "true"));
    EXPECT_FALSE(daemonOptionIsAccepted(policy, "sandbox", "false"));
    EXPECT_FALSE(daemonOptionIsAccepted(policy, "builders", "ssh-ng://builder"));
    EXPECT_TRUE(daemonOptionIsAccepted(policy, "builders", ""));
}

TEST(DaemonOptionPolicy, daemonPolicyFiltersOverrides)
{
    Settings settings;
    FileTransferSettings transfers;
    settings.set("timeout", "30");
    settings.set("sandbox", "false");
    settings.set("builders", "ssh-ng://builder");
    transfers.set("connect-timeout", "5");
    auto options = getForwardedDaemonOptions(settings, transfers, getDaemonOptionPolicy(settings, transfers, false));
    EXPECT_EQ(options.overrides, (StringMap{{"timeout", "30"}, {"connect-timeout", "5"}}));
    EXPECT_EQ(options.rejected, (StringSet{"sandbox", "builders"}));
    settings.set("builders", "");
    options = getForwardedDaemonOptions(settings, transfers, getDaemonOptionPolicy(settings, transfers, false));
    EXPECT_EQ(options.overrides.at("builders"), "");
}

TEST(DaemonOptionPolicy, absentPolicyUsesLegacyForwarding)
{
    Settings settings;
    FileTransferSettings transfers;
    settings.set("sandbox", "false");
    auto options = getForwardedDaemonOptions(settings, transfers, std::nullopt);
    EXPECT_EQ(options.overrides.at("sandbox"), "false");
    EXPECT_TRUE(options.rejected.empty());
}

TEST(DaemonOptionPolicy, emptyDaemonPolicyDoesNotMeanUnknown)
{
    Settings settings;
    FileTransferSettings transfers;
    settings.set("timeout", "30");
    auto options = getForwardedDaemonOptions(settings, transfers, StringMap{});
    EXPECT_TRUE(options.overrides.empty());
    EXPECT_EQ(options.rejected, (StringSet{"timeout"}));
}

TEST(DaemonOptionPolicy, aliasesAreSentUnderCanonicalNames)
{
    Settings settings;
    FileTransferSettings transfers;
    transfers.set("download-attempts", "7");
    auto options = getForwardedDaemonOptions(settings, transfers, std::nullopt);
    EXPECT_EQ(options.overrides.at("filetransfer-retry-attempts"), "7");
    EXPECT_FALSE(options.overrides.contains("download-attempts"));
}

TEST(DaemonOptionPolicy, allCurrentSettingsHaveAnExplicitClassification)
{
    Settings settings;
    FileTransferSettings transfers;
    std::map<std::string, Config::SettingInfo> known;
    settings.getSettings(known);
    transfers.getSettings(known);
    for (auto & [name, _] : known)
        EXPECT_TRUE(classifyDaemonOption(name).has_value()) << name;
}

#ifndef _WIN32
namespace {

struct OptionTestStore : UDSRemoteStore
{
    explicit OptionTestStore(ref<const UDSRemoteStoreConfig> config)
        : Store(*config)
        , LocalFSStore(*config)
        , RemoteStore(*config)
        , UDSRemoteStore(config)
    {
    }

    using RemoteStore::setOptions;
};

struct OptionTestConnection : RemoteStore::Connection
{
    void closeWrite() override
    {
        to.flush();
        shutdown(to.fd, SHUT_WR);
    }
};

struct RawDaemonClient : WorkerProto::BasicClientConnection
{
    void closeWrite() override
    {
        to.flush();
        shutdown(to.fd, SHUT_WR);
    }
};

void sendRawOptions(RawDaemonClient & conn, const StringMap & overrides)
{
    conn.to << WorkerProto::Op::SetOptions << settings.keepFailed << settings.getWorkerSettings().keepGoing
            << settings.getWorkerSettings().tryFallback << std::to_underlying(verbosity)
            << settings.getWorkerSettings().maxBuildJobs << settings.getWorkerSettings().maxSilentTime << true
            << std::to_underlying(settings.verboseBuild ? lvlError : lvlVomit) << 0 << 0
            << settings.getLocalSettings().buildCores << settings.getWorkerSettings().useSubstitutes
            << overrides.size();
    for (auto & [name, value] : overrides)
        conn.to << name << value;
    auto ex = conn.processStderrReturn();
    if (ex)
        std::rethrow_exception(ex);
}

WorkerProto::ClientHandshakeInfo sendRawOptionsToDaemon(
    ref<Store> store,
    TrustedFlag trusted,
    const StringMap & overrides,
    const WorkerProto::Version & clientVersion = WorkerProto::latest)
{
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0)
        throw SysError("creating socket pair");

    std::exception_ptr daemonException;
    auto daemonThread = std::jthread([store, fd = sockets[1], trusted, &daemonException]() mutable {
        try {
            daemon::processConnection(store, FdSource{fd}, FdSink{fd}, trusted, daemon::RecursiveFlag::NotRecursive);
        } catch (...) {
            daemonException = std::current_exception();
        }
    });

    WorkerProto::ClientHandshakeInfo handshakeInfo;
    {
        RawDaemonClient client;
        client.to = FdSink{sockets[0]};
        client.from = FdSource{sockets[0]};
        client.protoVersion = WorkerProto::BasicClientConnection::handshake(client.to, client.from, clientVersion);
        handshakeInfo = client.postHandshake(*store);
        if (auto ex = client.processStderrReturn())
            std::rethrow_exception(ex);
        sendRawOptions(client, overrides);
        client.closeWrite();
    }

    daemonThread.join();
    if (daemonException)
        std::rethrow_exception(daemonException);
    return handshakeInfo;
}

WorkerProto::ClientHandshakeInfo sendRemoteStoreOptionsToDaemon(
    OptionTestStore & clientStore,
    ref<Store> daemonStore,
    TrustedFlag trusted,
    const WorkerProto::Version & clientVersion)
{
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0)
        throw SysError("creating socket pair");

    std::exception_ptr daemonException;
    auto daemonThread = std::jthread([daemonStore, fd = sockets[1], trusted, &daemonException]() mutable {
        try {
            daemon::processConnection(
                daemonStore, FdSource{fd}, FdSink{fd}, trusted, daemon::RecursiveFlag::NotRecursive);
        } catch (...) {
            daemonException = std::current_exception();
        }
    });

    WorkerProto::ClientHandshakeInfo handshakeInfo;
    {
        OptionTestConnection client;
        client.to = FdSink{sockets[0]};
        client.from = FdSource{sockets[0]};
        client.protoVersion = WorkerProto::BasicClientConnection::handshake(client.to, client.from, clientVersion);
        static_cast<WorkerProto::ClientHandshakeInfo &>(client) = client.postHandshake(clientStore);
        if (auto ex = client.processStderrReturn())
            std::rethrow_exception(ex);

        clientStore.setOptions(client);
        handshakeInfo = client;
        client.closeWrite();
    }

    daemonThread.join();
    if (daemonException)
        std::rethrow_exception(daemonException);
    return handshakeInfo;
}

} // namespace

TEST(DaemonOptionPolicy, disabledOptionsDoNotWriteOnSubsequentCalls)
{
    auto config = make_ref<UDSRemoteStoreConfig>("/nonexistent-nix-option-policy-socket", StoreConfig::Params{});
    OptionTestStore store(config);
    OptionTestConnection conn;
    conn.protoVersion = WorkerProto::latest;
    conn.protoVersion.features.insert(std::string{WorkerProto::featureDisableSetOptions});
    EXPECT_NO_THROW(store.setOptions(conn));
    EXPECT_NO_THROW(store.setOptions(conn));
}

TEST(DaemonOptionPolicy, remoteStoreUsesLegacySetOptionsWhenPolicyIsNotNegotiated)
{
    auto daemonStore = make_ref<DummyStoreConfig>(DummyStoreConfig::Params{})->openDummyStore();
    auto clientConfig = make_ref<UDSRemoteStoreConfig>("/nonexistent-nix-option-policy-socket", StoreConfig::Params{});
    OptionTestStore clientStore(clientConfig);
    auto oldTimeout = settings.getWorkerSettings().buildTimeout.to_string();
    auto oldLogger = logger;
    Finally restore([&] {
        settings.set("timeout", oldTimeout);
        settings.resetOverridden();
        logger = oldLogger;
    });

    settings.set("timeout", "31");
    auto legacyVersion = WorkerProto::latest;
    legacyVersion.features.erase(std::string{WorkerProto::featureDaemonOptionPolicy});

    auto handshakeInfo = sendRemoteStoreOptionsToDaemon(clientStore, daemonStore, Trusted, legacyVersion);
    EXPECT_FALSE(handshakeInfo.daemonOptionPolicy);
    EXPECT_EQ(settings.getWorkerSettings().buildTimeout.to_string(), "31");
}

TEST(DaemonOptionPolicy, daemonEnforcesAdvertisedUntrustedPolicy)
{
    auto store = make_ref<DummyStoreConfig>(DummyStoreConfig::Params{})->openDummyStore();
    auto oldTimeout = settings.getWorkerSettings().buildTimeout.to_string();
    auto oldSandbox = settings.getLocalSettings().sandboxMode.to_string();
    auto oldLogger = logger;
    Finally restore([&] {
        settings.set("timeout", oldTimeout);
        settings.set("sandbox", oldSandbox);
        settings.resetOverridden();
        logger = oldLogger;
    });

    settings.set("timeout", "0");
    auto requestedSandbox = oldSandbox == "false" ? "true" : "false";

    auto handshakeInfo = sendRawOptionsToDaemon(store, NotTrusted, {{"timeout", "23"}, {"sandbox", requestedSandbox}});
    ASSERT_TRUE(handshakeInfo.daemonOptionPolicy);
    EXPECT_TRUE(handshakeInfo.daemonOptionPolicy->contains("timeout"));
    EXPECT_FALSE(handshakeInfo.daemonOptionPolicy->contains("sandbox"));

    EXPECT_EQ(settings.getWorkerSettings().buildTimeout.to_string(), "23");
    EXPECT_EQ(settings.getLocalSettings().sandboxMode.to_string(), oldSandbox);
}

TEST(DaemonOptionPolicy, trustedDaemonAppliesAdvertisedRestrictedOption)
{
    auto store = make_ref<DummyStoreConfig>(DummyStoreConfig::Params{})->openDummyStore();
    auto oldSandbox = settings.getLocalSettings().sandboxMode.to_string();
    auto oldLogger = logger;
    Finally restore([&] {
        settings.set("sandbox", oldSandbox);
        settings.resetOverridden();
        logger = oldLogger;
    });

    auto requestedSandbox = oldSandbox == "false" ? "true" : "false";
    auto handshakeInfo = sendRawOptionsToDaemon(store, Trusted, {{"sandbox", requestedSandbox}});
    ASSERT_TRUE(handshakeInfo.daemonOptionPolicy);
    EXPECT_TRUE(handshakeInfo.daemonOptionPolicy->contains("sandbox"));
    EXPECT_EQ(settings.getLocalSettings().sandboxMode.to_string(), requestedSandbox);
}

TEST(DaemonOptionPolicy, trustedDaemonRejectsOptionOutsideAdvertisedPolicy)
{
    auto store = make_ref<DummyStoreConfig>(DummyStoreConfig::Params{})->openDummyStore();
    auto oldUseXDGBaseDirectories = settings.useXDGBaseDirectories.to_string();
    auto oldLogger = logger;
    Finally restore([&] {
        settings.set("use-xdg-base-directories", oldUseXDGBaseDirectories);
        settings.resetOverridden();
        logger = oldLogger;
    });

    auto requestedUseXDGBaseDirectories = oldUseXDGBaseDirectories == "false" ? "true" : "false";
    auto handshakeInfo = sendRawOptionsToDaemon(
        store, Trusted, {{"use-xdg-base-directories", requestedUseXDGBaseDirectories}});
    ASSERT_TRUE(handshakeInfo.daemonOptionPolicy);
    EXPECT_FALSE(handshakeInfo.daemonOptionPolicy->contains("use-xdg-base-directories"));
    EXPECT_EQ(settings.useXDGBaseDirectories.to_string(), oldUseXDGBaseDirectories);
}

TEST(DaemonOptionPolicy, legacyTrustedClientPreservesOverrideBehavior)
{
    auto store = make_ref<DummyStoreConfig>(DummyStoreConfig::Params{})->openDummyStore();
    auto oldSandbox = settings.getLocalSettings().sandboxMode.to_string();
    auto oldLogger = logger;
    Finally restore([&] {
        settings.set("sandbox", oldSandbox);
        settings.resetOverridden();
        logger = oldLogger;
    });

    auto requestedSandbox = oldSandbox == "false" ? "true" : "false";
    WorkerProto::Version legacyVersion{.number = {.major = 1, .minor = 37}};
    auto handshakeInfo = sendRawOptionsToDaemon(store, Trusted, {{"sandbox", requestedSandbox}}, legacyVersion);
    EXPECT_FALSE(handshakeInfo.daemonOptionPolicy);
    EXPECT_EQ(settings.getLocalSettings().sandboxMode.to_string(), requestedSandbox);
}

TEST(DaemonOptionPolicy, legacyUntrustedClientPreservesRestrictions)
{
    auto store = make_ref<DummyStoreConfig>(DummyStoreConfig::Params{})->openDummyStore();
    auto oldTimeout = settings.getWorkerSettings().buildTimeout.to_string();
    auto oldSandbox = settings.getLocalSettings().sandboxMode.to_string();
    auto oldLogger = logger;
    Finally restore([&] {
        settings.set("timeout", oldTimeout);
        settings.set("sandbox", oldSandbox);
        settings.resetOverridden();
        logger = oldLogger;
    });

    settings.set("timeout", "0");
    auto requestedSandbox = oldSandbox == "false" ? "true" : "false";
    auto legacyVersion = WorkerProto::latest;
    legacyVersion.features.erase(std::string{WorkerProto::featureDaemonOptionPolicy});

    auto handshakeInfo =
        sendRawOptionsToDaemon(store, NotTrusted, {{"timeout", "29"}, {"sandbox", requestedSandbox}}, legacyVersion);
    EXPECT_FALSE(handshakeInfo.daemonOptionPolicy);
    EXPECT_EQ(settings.getWorkerSettings().buildTimeout.to_string(), "29");
    EXPECT_EQ(settings.getLocalSettings().sandboxMode.to_string(), oldSandbox);
}

TEST(DaemonOptionPolicy, untrustedDaemonAcceptsConfiguredSubstituter)
{
    auto store = make_ref<DummyStoreConfig>(DummyStoreConfig::Params{})->openDummyStore();
    auto oldSubstituters = settings.getWorkerSettings().substituters.to_string();
    auto oldLogger = logger;
    Finally restore([&] {
        settings.set("substituters", oldSubstituters);
        settings.resetOverridden();
        logger = oldLogger;
    });

    auto accepted = StoreReference::parse("https://allowed.example/");
    settings.set("substituters", "https://cache.nixos.org/ https://allowed.example/");
    settings.resetOverridden();

    auto handshakeInfo = sendRawOptionsToDaemon(store, NotTrusted, {{"substituters", accepted.render()}});
    ASSERT_TRUE(handshakeInfo.daemonOptionPolicy);
    EXPECT_EQ(settings.getWorkerSettings().substituters.get(), std::vector<StoreReference>{accepted});
}
#endif

} // namespace nix
