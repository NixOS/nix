#include <regex>

#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include "serve-protocol.hh"
#include "serve-protocol-impl.hh"
#include "build-result.hh"
#include "tests/protocol.hh"
#include "tests/characterization.hh"

namespace nix {

const char serveProtoDir[] = "serve-protocol";

struct ServeProtoTest : VersionedProtoTest<ServeProto, serveProtoDir>
{
    /**
     * For serializers that don't care about the minimum version, we
     * used the oldest one: 1.0.
     */
    ServeProto::Version defaultVersion = 2 << 8 | 0;
};

VERSIONED_CHARACTERIZATION_TEST(
    ServeProtoTest,
    string,
    "string",
    defaultVersion,
    (std::tuple<std::string, std::string, std::string, std::string, std::string> {
        "",
        "hi",
        "white rabbit",
        "大白兔",
        "oh no \0\0\0 what was that!",
    }))

VERSIONED_CHARACTERIZATION_TEST(
    ServeProtoTest,
    storePath,
    "store-path",
    defaultVersion,
    (std::tuple<StorePath, StorePath> {
        StorePath { "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo" },
        StorePath { "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo-bar" },
    }))

VERSIONED_CHARACTERIZATION_TEST(
    ServeProtoTest,
    contentAddress,
    "content-address",
    defaultVersion,
    (std::tuple<ContentAddress, ContentAddress, ContentAddress> {
        ContentAddress {
            .method = TextIngestionMethod {},
            .hash = hashString(HashAlgorithm::SHA256, "Derive(...)"),
        },
        ContentAddress {
            .method = FileIngestionMethod::Flat,
            .hash = hashString(HashAlgorithm::SHA1, "blob blob..."),
        },
        ContentAddress {
            .method = FileIngestionMethod::Recursive,
            .hash = hashString(HashAlgorithm::SHA256, "(...)"),
        },
    }))

VERSIONED_CHARACTERIZATION_TEST(
    ServeProtoTest,
    drvOutput,
    "drv-output",
    defaultVersion,
    (std::tuple<DrvOutput, DrvOutput> {
        {
            .drvHash = Hash::parseSRI("sha256-FePFYIlMuycIXPZbWi7LGEiMmZSX9FMbaQenWBzm1Sc="),
            .outputName = "baz",
        },
        DrvOutput {
            .drvHash = Hash::parseSRI("sha256-b4afnqKCO9oWXgYHb9DeQ2berSwOjS27rSd9TxXDc/U="),
            .outputName = "quux",
        },
    }))

VERSIONED_CHARACTERIZATION_TEST(
    ServeProtoTest,
    realisation,
    "realisation",
    defaultVersion,
    (std::tuple<Realisation, Realisation> {
        Realisation {
            .id = DrvOutput {
                .drvHash = Hash::parseSRI("sha256-FePFYIlMuycIXPZbWi7LGEiMmZSX9FMbaQenWBzm1Sc="),
                .outputName = "baz",
            },
            .outPath = StorePath { "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo" },
            .signatures = { "asdf", "qwer" },
        },
        Realisation {
            .id = {
                .drvHash = Hash::parseSRI("sha256-FePFYIlMuycIXPZbWi7LGEiMmZSX9FMbaQenWBzm1Sc="),
                .outputName = "baz",
            },
            .outPath = StorePath { "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo" },
            .signatures = { "asdf", "qwer" },
            .dependentRealisations = {
                {
                    DrvOutput {
                        .drvHash = Hash::parseSRI("sha256-b4afnqKCO9oWXgYHb9DeQ2berSwOjS27rSd9TxXDc/U="),
                        .outputName = "quux",
                    },
                    StorePath { "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo" },
                },
            },
        },
    }))

VERSIONED_CHARACTERIZATION_TEST(
    ServeProtoTest,
    buildResult_2_2,
    "build-result-2.2",
    2 << 8 | 2,
    ({
        using namespace std::literals::chrono_literals;
        std::tuple<BuildResult, BuildResult, BuildResult> t {
            BuildResult {
                .status = BuildResult::OutputRejected,
                .errorMsg = "no idea why",
            },
            BuildResult {
                .status = BuildResult::NotDeterministic,
                .errorMsg = "no idea why",
            },
            BuildResult {
                .status = BuildResult::Built,
            },
        };
        t;
    }))

VERSIONED_CHARACTERIZATION_TEST(
    ServeProtoTest,
    buildResult_2_3,
    "build-result-2.3",
    2 << 8 | 3,
    ({
        using namespace std::literals::chrono_literals;
        std::tuple<BuildResult, BuildResult, BuildResult> t {
            BuildResult {
                .status = BuildResult::OutputRejected,
                .errorMsg = "no idea why",
            },
            BuildResult {
                .status = BuildResult::NotDeterministic,
                .errorMsg = "no idea why",
                .timesBuilt = 3,
                .isNonDeterministic = true,
                .startTime = 30,
                .stopTime = 50,
            },
            BuildResult {
                .status = BuildResult::Built,
                .startTime = 30,
                .stopTime = 50,
            },
        };
        t;
    }))

VERSIONED_CHARACTERIZATION_TEST(
    ServeProtoTest,
    buildResult_2_6,
    "build-result-2.6",
    2 << 8 | 6,
    ({
        using namespace std::literals::chrono_literals;
        std::tuple<BuildResult, BuildResult, BuildResult> t {
            BuildResult {
                .status = BuildResult::OutputRejected,
                .errorMsg = "no idea why",
            },
            BuildResult {
                .status = BuildResult::NotDeterministic,
                .errorMsg = "no idea why",
                .timesBuilt = 3,
                .isNonDeterministic = true,
                .startTime = 30,
                .stopTime = 50,
            },
            BuildResult {
                .status = BuildResult::Built,
                .timesBuilt = 1,
                .builtOutputs = {
                    {
                        "foo",
                        {
                            .id = DrvOutput {
                                .drvHash = Hash::parseSRI("sha256-b4afnqKCO9oWXgYHb9DeQ2berSwOjS27rSd9TxXDc/U="),
                                .outputName = "foo",
                            },
                            .outPath = StorePath { "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo" },
                        },
                    },
                    {
                        "bar",
                        {
                            .id = DrvOutput {
                                .drvHash = Hash::parseSRI("sha256-b4afnqKCO9oWXgYHb9DeQ2berSwOjS27rSd9TxXDc/U="),
                                .outputName = "bar",
                            },
                            .outPath = StorePath { "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar" },
                        },
                    },
                },
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
    (std::tuple<UnkeyedValidPathInfo, UnkeyedValidPathInfo> {
        ({
            UnkeyedValidPathInfo info { Hash::dummy };
            info.narSize = 34878;
            info;
        }),
        ({
            UnkeyedValidPathInfo info { Hash::dummy };
            info.deriver = StorePath {
                "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar.drv",
            };
            info.references = {
                StorePath {
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
    (std::tuple<UnkeyedValidPathInfo, UnkeyedValidPathInfo> {
        ({
            UnkeyedValidPathInfo info {
                Hash::parseSRI("sha256-FePFYIlMuycIXPZbWi7LGEiMmZSX9FMbaQenWBzm1Sc="),
            };
            info.deriver = StorePath {
                "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar.drv",
            };
            info.references = {
                StorePath {
                    "g1w7hyyyy1w7hy3qg1w7hy3qgqqqqy3q-foo.drv",
                },
            };
            info.narSize = 34878;
            info;
        }),
        ({
            ValidPathInfo info {
                *LibStoreTest::store,
                "foo",
                FixedOutputInfo {
                    .method = FileIngestionMethod::Recursive,
                    .hash = hashString(HashAlgorithm::SHA256, "(...)"),
                    .references = {
                        .others = {
                            StorePath {
                                "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar",
                            },
                        },
                        .self = true,
                    },
                },
                Hash::parseSRI("sha256-FePFYIlMuycIXPZbWi7LGEiMmZSX9FMbaQenWBzm1Sc="),
            };
            info.deriver = StorePath {
                "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar.drv",
            };
            info.narSize = 34878;
            info.sigs = {
                "fake-sig-1",
                "fake-sig-2",
            },
            static_cast<UnkeyedValidPathInfo>(std::move(info));
        }),
    }))

VERSIONED_CHARACTERIZATION_TEST(
    ServeProtoTest,
    build_options_2_1,
    "build-options-2.1",
    2 << 8 | 1,
    (ServeProto::BuildOptions {
        .maxSilentTime = 5,
        .buildTimeout = 6,
    }))

VERSIONED_CHARACTERIZATION_TEST(
    ServeProtoTest,
    build_options_2_2,
    "build-options-2.2",
    2 << 8 | 2,
    (ServeProto::BuildOptions {
        .maxSilentTime = 5,
        .buildTimeout = 6,
        .maxLogSize = 7,
    }))

VERSIONED_CHARACTERIZATION_TEST(
    ServeProtoTest,
    build_options_2_3,
    "build-options-2.3",
    2 << 8 | 3,
    (ServeProto::BuildOptions {
        .maxSilentTime = 5,
        .buildTimeout = 6,
        .maxLogSize = 7,
        .nrRepeats = 8,
        .enforceDeterminism = true,
    }))

VERSIONED_CHARACTERIZATION_TEST(
    ServeProtoTest,
    build_options_2_7,
    "build-options-2.7",
    2 << 8 | 7,
    (ServeProto::BuildOptions {
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
    (std::tuple<std::vector<std::string>, std::vector<std::string>, std::vector<std::string>, std::vector<std::vector<std::string>>> {
        { },
        { "" },
        { "", "foo", "bar" },
        { {}, { "" }, { "", "1", "2" } },
    }))

VERSIONED_CHARACTERIZATION_TEST(
    ServeProtoTest,
    set,
    "set",
    defaultVersion,
    (std::tuple<std::set<std::string>, std::set<std::string>, std::set<std::string>, std::set<std::set<std::string>>> {
        { },
        { "" },
        { "", "foo", "bar" },
        { {}, { "" }, { "", "1", "2" } },
    }))

VERSIONED_CHARACTERIZATION_TEST(
    ServeProtoTest,
    optionalStorePath,
    "optional-store-path",
    defaultVersion,
    (std::tuple<std::optional<StorePath>, std::optional<StorePath>> {
        std::nullopt,
        std::optional {
            StorePath { "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo-bar" },
        },
    }))

VERSIONED_CHARACTERIZATION_TEST(
    ServeProtoTest,
    optionalContentAddress,
    "optional-content-address",
    defaultVersion,
    (std::tuple<std::optional<ContentAddress>, std::optional<ContentAddress>> {
        std::nullopt,
        std::optional {
            ContentAddress {
                .method = FileIngestionMethod::Flat,
                .hash = hashString(HashAlgorithm::SHA1, "blob blob..."),
            },
        },
    }))

}
