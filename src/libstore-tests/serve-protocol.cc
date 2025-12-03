#include <thread>
#include <regex>

#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include "nix/util/json-utils.hh"
#include "nix/store/serve-protocol.hh"
#include "nix/store/serve-protocol-impl.hh"
#include "nix/store/serve-protocol-connection.hh"
#include "nix/store/build-result.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/store/tests/protocol.hh"
#include "nix/util/tests/characterization.hh"

namespace nix {

const char serveProtoDir[] = "serve-protocol";

struct ServeProtoTest : VersionedProtoTest<ServeProto, serveProtoDir>
{
    /**
     * For serializers that don't care about the minimum version, we
     * used the oldest one: 2.5.
     */
    ServeProto::Version defaultVersion = 2 << 8 | 5;
};

VERSIONED_CHARACTERIZATION_TEST(
    ServeProtoTest,
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
    ServeProtoTest,
    storePath,
    "store-path",
    defaultVersion,
    (std::tuple<StorePath, StorePath>{
        StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo"},
        StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo-bar"},
    }))

VERSIONED_CHARACTERIZATION_TEST(
    ServeProtoTest,
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

VERSIONED_CHARACTERIZATION_TEST(
    ServeProtoTest,
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

#endif

VERSIONED_CHARACTERIZATION_TEST(
    ServeProtoTest,
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
    ServeProtoTest,
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

VERSIONED_CHARACTERIZATION_TEST(ServeProtoTest, buildResult_2_2, "build-result-2.2", 2 << 8 | 2, ({
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

VERSIONED_CHARACTERIZATION_TEST(ServeProtoTest, buildResult_2_3, "build-result-2.3", 2 << 8 | 3, ({
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
                                            }},
                                            .startTime = 30,
                                            .stopTime = 50,
                                        },
                                    };
                                    t;
                                }))

VERSIONED_CHARACTERIZATION_TEST(
    ServeProtoTest, buildResult_2_6, "build-result-2.6", 2 << 8 | 6, ({
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
#if 0
                // These fields are not yet serialized.
                // FIXME Include in next version of protocol or document
                // why they are skipped.
                .cpuUser = std::chrono::milliseconds(500s),
                .cpuSystem = std::chrono::milliseconds(604s),
#endif
            },
        };
        t;
    }))

VERSIONED_CHARACTERIZATION_TEST(
    ServeProtoTest,
    unkeyedValidPathInfo_2_3,
    "unkeyed-valid-path-info-2.3",
    2 << 8 | 3,
    (std::tuple<UnkeyedValidPathInfo, UnkeyedValidPathInfo>{
        ({
            UnkeyedValidPathInfo info{Hash::dummy};
            info.narSize = 34878;
            info;
        }),
        ({
            UnkeyedValidPathInfo info{Hash::dummy};
            info.deriver = StorePath{
                "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar.drv",
            };
            info.references = {
                StorePath{
                    "g1w7hyyyy1w7hy3qg1w7hy3qgqqqqy3q-foo.drv",
                },
            };
            info.narSize = 34878;
            info;
        }),
    }))

VERSIONED_CHARACTERIZATION_TEST(
    ServeProtoTest,
    unkeyedValidPathInfo_2_4,
    "unkeyed-valid-path-info-2.4",
    2 << 8 | 4,
    (std::tuple<UnkeyedValidPathInfo, UnkeyedValidPathInfo>{
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
            info.narSize = 34878;
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
            info.deriver = StorePath{
                "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar.drv",
            };
            info.narSize = 34878;
            info.sigs =
                {
                    "fake-sig-1",
                    "fake-sig-2",
                },
            static_cast<UnkeyedValidPathInfo>(std::move(info));
        }),
    }))

VERSIONED_CHARACTERIZATION_TEST_NO_JSON(
    ServeProtoTest,
    build_options_2_1,
    "build-options-2.1",
    2 << 8 | 1,
    (ServeProto::BuildOptions{
        .maxSilentTime = 5,
        .buildTimeout = 6,
    }))

VERSIONED_CHARACTERIZATION_TEST_NO_JSON(
    ServeProtoTest,
    build_options_2_2,
    "build-options-2.2",
    2 << 8 | 2,
    (ServeProto::BuildOptions{
        .maxSilentTime = 5,
        .buildTimeout = 6,
        .maxLogSize = 7,
    }))

VERSIONED_CHARACTERIZATION_TEST_NO_JSON(
    ServeProtoTest,
    build_options_2_3,
    "build-options-2.3",
    2 << 8 | 3,
    (ServeProto::BuildOptions{
        .maxSilentTime = 5,
        .buildTimeout = 6,
        .maxLogSize = 7,
        .nrRepeats = 8,
        .enforceDeterminism = true,
    }))

VERSIONED_CHARACTERIZATION_TEST_NO_JSON(
    ServeProtoTest,
    build_options_2_7,
    "build-options-2.7",
    2 << 8 | 7,
    (ServeProto::BuildOptions{
        .maxSilentTime = 5,
        .buildTimeout = 6,
        .maxLogSize = 7,
        .nrRepeats = 8,
        .enforceDeterminism = false,
        .keepFailed = true,
    }))

VERSIONED_CHARACTERIZATION_TEST(
    ServeProtoTest,
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
    ServeProtoTest,
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
    ServeProtoTest,
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
    ServeProtoTest,
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

TEST_F(ServeProtoTest, handshake_log)
{
    CharacterizationTest::writeTest("handshake-to-client.bin", [&]() -> std::string {
        StringSink toClientLog;

        Pipe toClient, toServer;
        toClient.create();
        toServer.create();

        ServeProto::Version clientResult;

        auto thread = std::thread([&]() {
            FdSink out{toServer.writeSide.get()};
            FdSource in0{toClient.readSide.get()};
            TeeSource in{in0, toClientLog};
            clientResult = ServeProto::BasicClientConnection::handshake(out, in, defaultVersion, "blah");
        });

        {
            FdSink out{toClient.writeSide.get()};
            FdSource in{toServer.readSide.get()};
            ServeProto::BasicServerConnection::handshake(out, in, defaultVersion);
        };

        thread.join();

        return std::move(toClientLog.s);
    });
}

/// Has to be a `BufferedSink` for handshake.
struct NullBufferedSink : BufferedSink
{
    void writeUnbuffered(std::string_view data) override {}
};

TEST_F(ServeProtoTest, handshake_client_replay)
{
    CharacterizationTest::readTest("handshake-to-client.bin", [&](std::string toClientLog) {
        NullBufferedSink nullSink;

        StringSource in{toClientLog};
        auto clientResult = ServeProto::BasicClientConnection::handshake(nullSink, in, defaultVersion, "blah");

        EXPECT_EQ(clientResult, defaultVersion);
    });
}

TEST_F(ServeProtoTest, handshake_client_truncated_replay_throws)
{
    CharacterizationTest::readTest("handshake-to-client.bin", [&](std::string toClientLog) {
        for (size_t len = 0; len < toClientLog.size(); ++len) {
            NullBufferedSink nullSink;
            auto substring = toClientLog.substr(0, len);
            StringSource in{substring};
            if (len < 8) {
                EXPECT_THROW(
                    ServeProto::BasicClientConnection::handshake(nullSink, in, defaultVersion, "blah"), EndOfFile);
            } else {
                // Not sure why cannot keep on checking for `EndOfFile`.
                EXPECT_THROW(ServeProto::BasicClientConnection::handshake(nullSink, in, defaultVersion, "blah"), Error);
            }
        }
    });
}

TEST_F(ServeProtoTest, handshake_client_corrupted_throws)
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
                EXPECT_THROW(ServeProto::BasicClientConnection::handshake(nullSink, in, defaultVersion, "blah"), Error);
            } else if (idx < 8 || idx >= 12) {
                // Number out of bounds
                EXPECT_THROW(
                    ServeProto::BasicClientConnection::handshake(nullSink, in, defaultVersion, "blah"),
                    SerialisationError);
            } else {
                auto ver = ServeProto::BasicClientConnection::handshake(nullSink, in, defaultVersion, "blah");
                // `std::min` of this and the other version saves us
                EXPECT_EQ(ver, defaultVersion);
            }
        }
    });
}

} // namespace nix
