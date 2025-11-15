#include <regex>
#include <thread>

#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include "nix/util/json-utils.hh"
#include "nix/store/worker-protocol.hh"
#include "nix/store/worker-protocol-connection.hh"
#include "nix/store/worker-protocol-impl.hh"
#include "nix/store/derived-path.hh"
#include "nix/store/build-result.hh"
#include "nix/store/tests/protocol.hh"
#include "nix/util/tests/characterization.hh"

namespace nix {

const char workerProtoDir[] = "worker-protocol";

struct WorkerProtoTest : VersionedProtoTest<WorkerProto, workerProtoDir>
{
    /**
     * For serializers that don't care about the minimum version, we
     * used the oldest one: 1.10.
     */
    WorkerProto::Version defaultVersion = 1 << 8 | 10;
};

VERSIONED_CHARACTERIZATION_TEST(
    WorkerProtoTest,
    string,
    "string",
    defaultVersion,
    (std::tuple<std::string, std::string, std::string, std::string, std::string>{
        "",
        "hi",
        "white rabbit",
        "大白兔",
        "oh no \0\0\0 what was that!",
    }))

#ifndef DOXYGEN_SKIP

VERSIONED_CHARACTERIZATION_TEST(
    WorkerProtoTest,
    storePath,
    "store-path",
    defaultVersion,
    (std::tuple<StorePath, StorePath>{
        StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo"},
        StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo-bar"},
    }))

VERSIONED_CHARACTERIZATION_TEST(
    WorkerProtoTest,
    contentAddress,
    "content-address",
    defaultVersion,
    (std::tuple<ContentAddress, ContentAddress, ContentAddress>{
        ContentAddress{
            .method = ContentAddressMethod::Raw::Text,
            .hash = hashString(HashAlgorithm::SHA256, "Derive(...)"),
        },
        ContentAddress{
            .method = ContentAddressMethod::Raw::Flat,
            .hash = hashString(HashAlgorithm::SHA1, "blob blob..."),
        },
        ContentAddress{
            .method = ContentAddressMethod::Raw::NixArchive,
            .hash = hashString(HashAlgorithm::SHA256, "(...)"),
        },
    }))

#endif

VERSIONED_CHARACTERIZATION_TEST(
    WorkerProtoTest,
    derivedPath_1_29,
    "derived-path-1.29",
    1 << 8 | 29,
    (std::tuple<DerivedPath, DerivedPath, DerivedPath>{
        DerivedPath::Opaque{
            .path = StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo"},
        },
        DerivedPath::Built{
            .drvPath = makeConstantStorePathRef(
                StorePath{
                    "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar.drv",
                }),
            .outputs = OutputsSpec::All{},
        },
        DerivedPath::Built{
            .drvPath = makeConstantStorePathRef(
                StorePath{
                    "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar.drv",
                }),
            .outputs = OutputsSpec::Names{"x", "y"},
        },
    }))

VERSIONED_CHARACTERIZATION_TEST(
    WorkerProtoTest,
    derivedPath_1_30,
    "derived-path-1.30",
    1 << 8 | 30,
    (std::tuple<DerivedPath, DerivedPath, DerivedPath, DerivedPath>{
        DerivedPath::Opaque{
            .path = StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo"},
        },
        DerivedPath::Opaque{
            .path = StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo.drv"},
        },
        DerivedPath::Built{
            .drvPath = makeConstantStorePathRef(
                StorePath{
                    "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar.drv",
                }),
            .outputs = OutputsSpec::All{},
        },
        DerivedPath::Built{
            .drvPath = makeConstantStorePathRef(
                StorePath{
                    "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar.drv",
                }),
            .outputs = OutputsSpec::Names{"x", "y"},
        },
    }))

VERSIONED_CHARACTERIZATION_TEST(
    WorkerProtoTest,
    drvOutput,
    "drv-output",
    defaultVersion,
    (std::tuple<DrvOutput, DrvOutput>{
        {
            .drvHash = Hash::parseSRI("sha256-FePFYIlMuycIXPZbWi7LGEiMmZSX9FMbaQenWBzm1Sc="),
            .outputName = "baz",
        },
        DrvOutput{
            .drvHash = Hash::parseSRI("sha256-b4afnqKCO9oWXgYHb9DeQ2berSwOjS27rSd9TxXDc/U="),
            .outputName = "quux",
        },
    }))

VERSIONED_CHARACTERIZATION_TEST(
    WorkerProtoTest,
    realisation,
    "realisation",
    defaultVersion,
    (std::tuple<Realisation, Realisation>{
        Realisation{
            {
                .outPath = StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo"},
            },
            {
                .drvHash = Hash::parseSRI("sha256-FePFYIlMuycIXPZbWi7LGEiMmZSX9FMbaQenWBzm1Sc="),
                .outputName = "baz",
            },
        },
        Realisation{
            {
                .outPath = StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo"},
                .signatures = {"asdf", "qwer"},
            },
            {
                .drvHash = Hash::parseSRI("sha256-FePFYIlMuycIXPZbWi7LGEiMmZSX9FMbaQenWBzm1Sc="),
                .outputName = "baz",
            },
        },
    }))

VERSIONED_CHARACTERIZATION_TEST(
    WorkerProtoTest,
    realisation_with_deps,
    "realisation-with-deps",
    defaultVersion,
    (std::tuple<Realisation>{
        Realisation{
            {
                .outPath = StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo"},
                .signatures = {"asdf", "qwer"},
                .dependentRealisations =
                    {
                        {
                            DrvOutput{
                                .drvHash = Hash::parseSRI("sha256-b4afnqKCO9oWXgYHb9DeQ2berSwOjS27rSd9TxXDc/U="),
                                .outputName = "quux",
                            },
                            StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo"},
                        },
                    },
            },
            {
                .drvHash = Hash::parseSRI("sha256-FePFYIlMuycIXPZbWi7LGEiMmZSX9FMbaQenWBzm1Sc="),
                .outputName = "baz",
            },
        },
    }))

VERSIONED_CHARACTERIZATION_TEST(WorkerProtoTest, buildResult_1_27, "build-result-1.27", 1 << 8 | 27, ({
                                    using namespace std::literals::chrono_literals;
                                    std::tuple<BuildResult, BuildResult, BuildResult> t{
                                        BuildResult{.inner{BuildResult::Failure{
                                            .status = BuildResult::Failure::OutputRejected,
                                            .errorMsg = "no idea why",
                                        }}},
                                        BuildResult{.inner{BuildResult::Failure{
                                            .status = BuildResult::Failure::NotDeterministic,
                                            .errorMsg = "no idea why",
                                        }}},
                                        BuildResult{.inner{BuildResult::Success{
                                            .status = BuildResult::Success::Built,
                                        }}},
                                    };
                                    t;
                                }))

VERSIONED_CHARACTERIZATION_TEST(
    WorkerProtoTest, buildResult_1_28, "build-result-1.28", 1 << 8 | 28, ({
        using namespace std::literals::chrono_literals;
        std::tuple<BuildResult, BuildResult, BuildResult> t{
            BuildResult{.inner{BuildResult::Failure{
                .status = BuildResult::Failure::OutputRejected,
                .errorMsg = "no idea why",
            }}},
            BuildResult{.inner{BuildResult::Failure{
                .status = BuildResult::Failure::NotDeterministic,
                .errorMsg = "no idea why",
            }}},
            BuildResult{.inner{BuildResult::Success{
                .status = BuildResult::Success::Built,
                .builtOutputs =
                    {
                        {
                            "foo",
                            {
                                {
                                    .outPath = StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo"},
                                },
                                DrvOutput{
                                    .drvHash = Hash::parseSRI("sha256-b4afnqKCO9oWXgYHb9DeQ2berSwOjS27rSd9TxXDc/U="),
                                    .outputName = "foo",
                                },
                            },
                        },
                        {
                            "bar",
                            {
                                {
                                    .outPath = StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar"},
                                },
                                DrvOutput{
                                    .drvHash = Hash::parseSRI("sha256-b4afnqKCO9oWXgYHb9DeQ2berSwOjS27rSd9TxXDc/U="),
                                    .outputName = "bar",
                                },
                            },
                        },
                    },
            }}},
        };
        t;
    }))

VERSIONED_CHARACTERIZATION_TEST(
    WorkerProtoTest, buildResult_1_29, "build-result-1.29", 1 << 8 | 29, ({
        using namespace std::literals::chrono_literals;
        std::tuple<BuildResult, BuildResult, BuildResult> t{
            BuildResult{.inner{BuildResult::Failure{
                .status = BuildResult::Failure::OutputRejected,
                .errorMsg = "no idea why",
            }}},
            BuildResult{
                .inner{BuildResult::Failure{
                    .status = BuildResult::Failure::NotDeterministic,
                    .errorMsg = "no idea why",
                    .isNonDeterministic = true,
                }},
                .timesBuilt = 3,
                .startTime = 30,
                .stopTime = 50,
            },
            BuildResult{
                .inner{BuildResult::Success{
                    .status = BuildResult::Success::Built,
                    .builtOutputs =
                        {
                            {
                                "foo",
                                {
                                    {
                                        .outPath = StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo"},
                                    },
                                    DrvOutput{
                                        .drvHash =
                                            Hash::parseSRI("sha256-b4afnqKCO9oWXgYHb9DeQ2berSwOjS27rSd9TxXDc/U="),
                                        .outputName = "foo",
                                    },
                                },
                            },
                            {
                                "bar",
                                {
                                    {
                                        .outPath = StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar"},
                                    },
                                    DrvOutput{
                                        .drvHash =
                                            Hash::parseSRI("sha256-b4afnqKCO9oWXgYHb9DeQ2berSwOjS27rSd9TxXDc/U="),
                                        .outputName = "bar",
                                    },
                                },
                            },
                        },
                }},
                .timesBuilt = 1,
                .startTime = 30,
                .stopTime = 50,
            },
        };
        t;
    }))

VERSIONED_CHARACTERIZATION_TEST(
    WorkerProtoTest, buildResult_1_37, "build-result-1.37", 1 << 8 | 37, ({
        using namespace std::literals::chrono_literals;
        std::tuple<BuildResult, BuildResult, BuildResult> t{
            BuildResult{.inner{BuildResult::Failure{
                .status = BuildResult::Failure::OutputRejected,
                .errorMsg = "no idea why",
            }}},
            BuildResult{
                .inner{BuildResult::Failure{
                    .status = BuildResult::Failure::NotDeterministic,
                    .errorMsg = "no idea why",
                    .isNonDeterministic = true,
                }},
                .timesBuilt = 3,
                .startTime = 30,
                .stopTime = 50,
            },
            BuildResult{
                .inner{BuildResult::Success{
                    .status = BuildResult::Success::Built,
                    .builtOutputs =
                        {
                            {
                                "foo",
                                {
                                    {
                                        .outPath = StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo"},
                                    },
                                    DrvOutput{
                                        .drvHash =
                                            Hash::parseSRI("sha256-b4afnqKCO9oWXgYHb9DeQ2berSwOjS27rSd9TxXDc/U="),
                                        .outputName = "foo",
                                    },
                                },
                            },
                            {
                                "bar",
                                {
                                    {
                                        .outPath = StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar"},
                                    },
                                    DrvOutput{
                                        .drvHash =
                                            Hash::parseSRI("sha256-b4afnqKCO9oWXgYHb9DeQ2berSwOjS27rSd9TxXDc/U="),
                                        .outputName = "bar",
                                    },
                                },
                            },
                        },
                }},
                .timesBuilt = 1,
                .startTime = 30,
                .stopTime = 50,
                .cpuUser = std::chrono::microseconds(500s),
                .cpuSystem = std::chrono::microseconds(604s),
            },
        };
        t;
    }))

VERSIONED_CHARACTERIZATION_TEST(WorkerProtoTest, keyedBuildResult_1_29, "keyed-build-result-1.29", 1 << 8 | 29, ({
                                    using namespace std::literals::chrono_literals;
                                    std::tuple<KeyedBuildResult, KeyedBuildResult /*, KeyedBuildResult*/> t{
                                        KeyedBuildResult{
                                            {.inner{BuildResult::Failure{
                                                .status = KeyedBuildResult::Failure::OutputRejected,
                                                .errorMsg = "no idea why",
                                            }}},
                                            /* .path = */
                                            DerivedPath::Opaque{
                                                StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-xxx"},
                                            },
                                        },
                                        KeyedBuildResult{
                                            {
                                                .inner{BuildResult::Failure{
                                                    .status = KeyedBuildResult::Failure::NotDeterministic,
                                                    .errorMsg = "no idea why",
                                                    .isNonDeterministic = true,
                                                }},
                                                .timesBuilt = 3,
                                                .startTime = 30,
                                                .stopTime = 50,
                                            },
                                            /* .path = */
                                            DerivedPath::Built{
                                                .drvPath = makeConstantStorePathRef(
                                                    StorePath{
                                                        "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar.drv",
                                                    }),
                                                .outputs = OutputsSpec::Names{"out"},
                                            },
                                        },
                                    };
                                    t;
                                }))

VERSIONED_CHARACTERIZATION_TEST(
    WorkerProtoTest,
    unkeyedValidPathInfo_1_15,
    "unkeyed-valid-path-info-1.15",
    1 << 8 | 15,
    (std::tuple<UnkeyedValidPathInfo, UnkeyedValidPathInfo>{
        ({
            UnkeyedValidPathInfo info{
                Hash::parseSRI("sha256-FePFYIlMuycIXPZbWi7LGEiMmZSX9FMbaQenWBzm1Sc="),
            };
            info.registrationTime = 23423;
            info.narSize = 34878;
            info;
        }),
        ({
            UnkeyedValidPathInfo info{
                Hash::parseSRI("sha256-FePFYIlMuycIXPZbWi7LGEiMmZSX9FMbaQenWBzm1Sc="),
            };
            info.deriver = StorePath{
                "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar.drv",
            };
            info.references = {
                StorePath{
                    "g1w7hyyyy1w7hy3qg1w7hy3qgqqqqy3q-foo.drv",
                },
            };
            info.registrationTime = 23423;
            info.narSize = 34878;
            info;
        }),
    }))

VERSIONED_CHARACTERIZATION_TEST(
    WorkerProtoTest,
    validPathInfo_1_15,
    "valid-path-info-1.15",
    1 << 8 | 15,
    (std::tuple<ValidPathInfo, ValidPathInfo>{
        ({
            ValidPathInfo info{
                StorePath{
                    "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar",
                },
                UnkeyedValidPathInfo{
                    Hash::parseSRI("sha256-FePFYIlMuycIXPZbWi7LGEiMmZSX9FMbaQenWBzm1Sc="),
                },
            };
            info.registrationTime = 23423;
            info.narSize = 34878;
            info;
        }),
        ({
            ValidPathInfo info{
                StorePath{
                    "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar",
                },
                UnkeyedValidPathInfo{
                    Hash::parseSRI("sha256-FePFYIlMuycIXPZbWi7LGEiMmZSX9FMbaQenWBzm1Sc="),
                },
            };
            info.deriver = StorePath{
                "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar.drv",
            };
            info.references = {
                // other reference
                StorePath{
                    "g1w7hyyyy1w7hy3qg1w7hy3qgqqqqy3q-foo",
                },
                // self reference
                StorePath{
                    "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar",
                },
            };
            info.registrationTime = 23423;
            info.narSize = 34878;
            info;
        }),
    }))

VERSIONED_CHARACTERIZATION_TEST(
    WorkerProtoTest,
    validPathInfo_1_16,
    "valid-path-info-1.16",
    1 << 8 | 16,
    (std::tuple<ValidPathInfo, ValidPathInfo, ValidPathInfo>{
        ({
            ValidPathInfo info{
                StorePath{
                    "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar",
                },
                UnkeyedValidPathInfo{
                    Hash::parseSRI("sha256-FePFYIlMuycIXPZbWi7LGEiMmZSX9FMbaQenWBzm1Sc="),
                },
            };
            info.registrationTime = 23423;
            info.narSize = 34878;
            info.ultimate = true;
            info;
        }),
        ({
            ValidPathInfo info{
                StorePath{
                    "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar",
                },
                UnkeyedValidPathInfo{
                    Hash::parseSRI("sha256-FePFYIlMuycIXPZbWi7LGEiMmZSX9FMbaQenWBzm1Sc="),
                },
            };
            info.deriver = StorePath{
                "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar.drv",
            };
            info.references = {
                // other reference
                StorePath{
                    "g1w7hyyyy1w7hy3qg1w7hy3qgqqqqy3q-foo",
                },
                // self reference
                StorePath{
                    "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar",
                },
            };
            info.registrationTime = 23423;
            info.narSize = 34878;
            info.sigs =
                {
                    "fake-sig-1",
                    "fake-sig-2",
                },
            info;
        }),
        ({
            auto info = ValidPathInfo::makeFromCA(
                store,
                "foo",
                FixedOutputInfo{
                    .method = FileIngestionMethod::NixArchive,
                    .hash = hashString(HashAlgorithm::SHA256, "(...)"),
                    .references =
                        {
                            .others =
                                {
                                    StorePath{
                                        "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar",
                                    },
                                },
                            .self = true,
                        },
                },
                Hash::parseSRI("sha256-FePFYIlMuycIXPZbWi7LGEiMmZSX9FMbaQenWBzm1Sc="));
            info.registrationTime = 23423;
            info.narSize = 34878;
            info;
        }),
    }))

VERSIONED_CHARACTERIZATION_TEST(
    WorkerProtoTest,
    buildMode,
    "build-mode",
    defaultVersion,
    (std::tuple<BuildMode, BuildMode, BuildMode>{
        bmNormal,
        bmRepair,
        bmCheck,
    }))

VERSIONED_CHARACTERIZATION_TEST(
    WorkerProtoTest,
    optionalTrustedFlag,
    "optional-trusted-flag",
    defaultVersion,
    (std::tuple<std::optional<TrustedFlag>, std::optional<TrustedFlag>, std::optional<TrustedFlag>>{
        std::nullopt,
        std::optional{Trusted},
        std::optional{NotTrusted},
    }))

VERSIONED_CHARACTERIZATION_TEST(
    WorkerProtoTest,
    vector,
    "vector",
    defaultVersion,
    (std::tuple<
        std::vector<std::string>,
        std::vector<std::string>,
        std::vector<std::string>,
        std::vector<std::vector<std::string>>>{
        {},
        {""},
        {"", "foo", "bar"},
        {{}, {""}, {"", "1", "2"}},
    }))

VERSIONED_CHARACTERIZATION_TEST(
    WorkerProtoTest,
    set,
    "set",
    defaultVersion,
    (std::tuple<StringSet, StringSet, StringSet, std::set<StringSet>>{
        {},
        {""},
        {"", "foo", "bar"},
        {{}, {""}, {"", "1", "2"}},
    }))

VERSIONED_CHARACTERIZATION_TEST(
    WorkerProtoTest,
    optionalStorePath,
    "optional-store-path",
    defaultVersion,
    (std::tuple<std::optional<StorePath>, std::optional<StorePath>>{
        std::nullopt,
        std::optional{
            StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo-bar"},
        },
    }))

VERSIONED_CHARACTERIZATION_TEST(
    WorkerProtoTest,
    optionalContentAddress,
    "optional-content-address",
    defaultVersion,
    (std::tuple<std::optional<ContentAddress>, std::optional<ContentAddress>>{
        std::nullopt,
        std::optional{
            ContentAddress{
                .method = ContentAddressMethod::Raw::Flat,
                .hash = hashString(HashAlgorithm::SHA1, "blob blob..."),
            },
        },
    }))

VERSIONED_CHARACTERIZATION_TEST_NO_JSON(
    WorkerProtoTest,
    clientHandshakeInfo_1_30,
    "client-handshake-info_1_30",
    1 << 8 | 30,
    (std::tuple<WorkerProto::ClientHandshakeInfo>{
        {},
    }))

VERSIONED_CHARACTERIZATION_TEST_NO_JSON(
    WorkerProtoTest,
    clientHandshakeInfo_1_33,
    "client-handshake-info_1_33",
    1 << 8 | 33,
    (std::tuple<WorkerProto::ClientHandshakeInfo, WorkerProto::ClientHandshakeInfo>{
        {
            .daemonNixVersion = std::optional{"foo"},
        },
        {
            .daemonNixVersion = std::optional{"bar"},
        },
    }))

VERSIONED_CHARACTERIZATION_TEST_NO_JSON(
    WorkerProtoTest,
    clientHandshakeInfo_1_35,
    "client-handshake-info_1_35",
    1 << 8 | 35,
    (std::tuple<WorkerProto::ClientHandshakeInfo, WorkerProto::ClientHandshakeInfo>{
        {
            .daemonNixVersion = std::optional{"foo"},
            .remoteTrustsUs = std::optional{NotTrusted},
        },
        {
            .daemonNixVersion = std::optional{"bar"},
            .remoteTrustsUs = std::optional{Trusted},
        },
    }))

TEST_F(WorkerProtoTest, handshake_log)
{
    CharacterizationTest::writeTest("handshake-to-client.bin", [&]() -> std::string {
        StringSink toClientLog;

        Pipe toClient, toServer;
        toClient.create();
        toServer.create();

        WorkerProto::Version clientResult;

        auto thread = std::thread([&]() {
            FdSink out{toServer.writeSide.get()};
            FdSource in0{toClient.readSide.get()};
            TeeSource in{in0, toClientLog};
            clientResult = std::get<0>(WorkerProto::BasicClientConnection::handshake(out, in, defaultVersion, {}));
        });

        {
            FdSink out{toClient.writeSide.get()};
            FdSource in{toServer.readSide.get()};
            WorkerProto::BasicServerConnection::handshake(out, in, defaultVersion, {});
        };

        thread.join();

        return std::move(toClientLog.s);
    });
}

TEST_F(WorkerProtoTest, handshake_features)
{
    Pipe toClient, toServer;
    toClient.create();
    toServer.create();

    std::tuple<WorkerProto::Version, WorkerProto::FeatureSet> clientResult;

    auto clientThread = std::thread([&]() {
        FdSink out{toServer.writeSide.get()};
        FdSource in{toClient.readSide.get()};
        clientResult = WorkerProto::BasicClientConnection::handshake(out, in, 123, {"bar", "aap", "mies", "xyzzy"});
    });

    FdSink out{toClient.writeSide.get()};
    FdSource in{toServer.readSide.get()};
    auto daemonResult = WorkerProto::BasicServerConnection::handshake(out, in, 456, {"foo", "bar", "xyzzy"});

    clientThread.join();

    EXPECT_EQ(clientResult, daemonResult);
    EXPECT_EQ(std::get<0>(clientResult), 123u);
    EXPECT_EQ(std::get<1>(clientResult), WorkerProto::FeatureSet({"bar", "xyzzy"}));
}

/// Has to be a `BufferedSink` for handshake.
struct NullBufferedSink : BufferedSink
{
    void writeUnbuffered(std::string_view data) override {}
};

TEST_F(WorkerProtoTest, handshake_client_replay)
{
    CharacterizationTest::readTest("handshake-to-client.bin", [&](std::string toClientLog) {
        NullBufferedSink nullSink;

        StringSource in{toClientLog};
        auto clientResult =
            std::get<0>(WorkerProto::BasicClientConnection::handshake(nullSink, in, defaultVersion, {}));

        EXPECT_EQ(clientResult, defaultVersion);
    });
}

TEST_F(WorkerProtoTest, handshake_client_truncated_replay_throws)
{
    CharacterizationTest::readTest("handshake-to-client.bin", [&](std::string toClientLog) {
        for (size_t len = 0; len < toClientLog.size(); ++len) {
            NullBufferedSink nullSink;
            auto substring = toClientLog.substr(0, len);
            StringSource in{substring};
            if (len < 8) {
                EXPECT_THROW(
                    WorkerProto::BasicClientConnection::handshake(nullSink, in, defaultVersion, {}), EndOfFile);
            } else {
                // Not sure why cannot keep on checking for `EndOfFile`.
                EXPECT_THROW(WorkerProto::BasicClientConnection::handshake(nullSink, in, defaultVersion, {}), Error);
            }
        }
    });
}

TEST_F(WorkerProtoTest, handshake_client_corrupted_throws)
{
    CharacterizationTest::readTest("handshake-to-client.bin", [&](const std::string toClientLog) {
        for (size_t idx = 0; idx < toClientLog.size(); ++idx) {
            // corrupt a copy
            std::string toClientLogCorrupt = toClientLog;
            toClientLogCorrupt[idx] *= 4;
            ++toClientLogCorrupt[idx];

            NullBufferedSink nullSink;
            StringSource in{toClientLogCorrupt};

            if (idx < 4 || idx == 9) {
                // magic bytes don't match
                EXPECT_THROW(WorkerProto::BasicClientConnection::handshake(nullSink, in, defaultVersion, {}), Error);
            } else if (idx < 8 || idx >= 12) {
                // Number out of bounds
                EXPECT_THROW(
                    WorkerProto::BasicClientConnection::handshake(nullSink, in, defaultVersion, {}),
                    SerialisationError);
            } else {
                auto ver = std::get<0>(WorkerProto::BasicClientConnection::handshake(nullSink, in, defaultVersion, {}));
                // `std::min` of this and the other version saves us
                EXPECT_EQ(ver, defaultVersion);
            }
        }
    });
}

} // namespace nix
