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

TEST(WorkerProtoVersionNumber, ordering)
{
    using Number = WorkerProto::Version::Number;
    EXPECT_LT((Number{1, 10}), (Number{1, 20}));
    EXPECT_GT((Number{1, 30}), (Number{1, 20}));
    EXPECT_EQ((Number{1, 10}), (Number{1, 10}));
    EXPECT_LT((Number{0, 255}), (Number{1, 0}));
}

TEST(WorkerProtoVersion, partialOrderingSameFeatures)
{
    using V = WorkerProto::Version;
    V v1{.number = {1, 20}, .features = {"a", "b"}};
    V v2{.number = {1, 30}, .features = {"a", "b"}};

    EXPECT_TRUE(v1 < v2);
    EXPECT_TRUE(v2 > v1);
    EXPECT_TRUE(v1 <= v2);
    EXPECT_TRUE(v2 >= v1);
    EXPECT_FALSE(v1 == v2);
}

TEST(WorkerProtoVersion, partialOrderingSubsetFeatures)
{
    using V = WorkerProto::Version;
    V fewer{.number = {1, 30}, .features = {"a"}};
    V more{.number = {1, 30}, .features = {"a", "b"}};

    // fewer <= more: JUST the features are a subset
    EXPECT_TRUE(fewer < more);
    EXPECT_TRUE(fewer <= more);
    EXPECT_FALSE(fewer > more);
    EXPECT_TRUE(fewer != more);
}

TEST(WorkerProtoVersion, partialOrderingUnordered)
{
    using V = WorkerProto::Version;
    // Same number but incomparable features
    V v1{.number = {1, 20}, .features = {"a", "c"}};
    V v2{.number = {1, 20}, .features = {"a", "b"}};

    EXPECT_FALSE(v1 < v2);
    EXPECT_FALSE(v1 > v2);
    EXPECT_FALSE(v1 <= v2);
    EXPECT_FALSE(v1 >= v2);
    EXPECT_FALSE(v1 == v2);
    EXPECT_TRUE(v1 != v2);
}

TEST(WorkerProtoVersion, partialOrderingHigherNumberFewerFeatures)
{
    using V = WorkerProto::Version;
    // Higher number but fewer features — unordered
    V v1{.number = {1, 30}, .features = {"a"}};
    V v2{.number = {1, 20}, .features = {"a", "b"}};

    EXPECT_FALSE(v1 < v2);
    EXPECT_FALSE(v1 > v2);
    EXPECT_FALSE(v1 == v2);
}

TEST(WorkerProtoVersion, partialOrderingEmptyFeatures)
{
    using V = WorkerProto::Version;
    V empty{.number = {1, 20}, .features = {}};
    V some{.number = {1, 30}, .features = {"a"}};

    // empty features is a subset of everything
    EXPECT_TRUE(empty < some);
    EXPECT_TRUE(empty <= some);
    EXPECT_TRUE(empty != some);
}

const char workerProtoDir[] = "worker-protocol";

static constexpr std::string_view defaultStoreDir = "/nix/store";

struct WorkerProtoTest : VersionedProtoTest<WorkerProto, workerProtoDir>
{
    /**
     * For serializers that don't care about the minimum version, we
     * used the oldest one: 1.10.
     */
    WorkerProto::Version defaultVersion = {
        .number =
            {
                .major = 1,
                .minor = 10,
            },
    };
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
    (WorkerProto::Version{
        .number =
            {
                .major = 1,
                .minor = 29,
            },
    }),
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
    (WorkerProto::Version{
        .number =
            {
                .major = 1,
                .minor = 30,
            },
    }),
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
                .signatures =
                    {
                        Signature{.keyName = "asdf", .sig = std::string(64, '\0')},
                        Signature{.keyName = "qwer", .sig = std::string(64, '\0')},
                    },
            },
            {
                .drvHash = Hash::parseSRI("sha256-FePFYIlMuycIXPZbWi7LGEiMmZSX9FMbaQenWBzm1Sc="),
                .outputName = "baz",
            },
        },
    }))

VERSIONED_READ_CHARACTERIZATION_TEST(
    WorkerProtoTest,
    realisation_with_deps,
    "realisation-with-deps",
    defaultVersion,
    (std::tuple<Realisation>{
        Realisation{
            {
                .outPath = StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo"},
                .signatures =
                    {
                        Signature{.keyName = "asdf", .sig = std::string(64, '\0')},
                        Signature{.keyName = "qwer", .sig = std::string(64, '\0')},
                    },
            },
            {
                .drvHash = Hash::parseSRI("sha256-FePFYIlMuycIXPZbWi7LGEiMmZSX9FMbaQenWBzm1Sc="),
                .outputName = "baz",
            },
        },
    }))

VERSIONED_CHARACTERIZATION_TEST(
    WorkerProtoTest,
    buildResult_1_27,
    "build-result-1.27",
    (WorkerProto::Version{
        .number =
            {
                .major = 1,
                .minor = 27,
            },
    }),
    ({
        using namespace std::literals::chrono_literals;
        std::tuple<BuildResult, BuildResult, BuildResult> t{
            BuildResult{.inner{BuildResult::Failure{{
                .status = BuildResult::Failure::OutputRejected,
                .msg = HintFmt("no idea why"),
            }}}},
            BuildResult{.inner{BuildResult::Failure{{
                .status = BuildResult::Failure::NotDeterministic,
                .msg = HintFmt("no idea why"),
            }}}},
            BuildResult{.inner{BuildResult::Success{
                .status = BuildResult::Success::Built,
            }}},
        };
        t;
    }))

VERSIONED_CHARACTERIZATION_TEST(
    WorkerProtoTest,
    buildResult_1_28,
    "build-result-1.28",
    (WorkerProto::Version{
        .number =
            {
                .major = 1,
                .minor = 28,
            },
    }),
    ({
        using namespace std::literals::chrono_literals;
        std::tuple<BuildResult, BuildResult, BuildResult> t{
            BuildResult{.inner{BuildResult::Failure{{
                .status = BuildResult::Failure::OutputRejected,
                .msg = HintFmt("no idea why"),
            }}}},
            BuildResult{.inner{BuildResult::Failure{{
                .status = BuildResult::Failure::NotDeterministic,
                .msg = HintFmt("no idea why"),
            }}}},
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
    WorkerProtoTest,
    buildResult_1_29,
    "build-result-1.29",
    (WorkerProto::Version{
        .number =
            {
                .major = 1,
                .minor = 29,
            },
    }),
    ({
        using namespace std::literals::chrono_literals;
        std::tuple<BuildResult, BuildResult, BuildResult> t{
            BuildResult{.inner{BuildResult::Failure{{
                .status = BuildResult::Failure::OutputRejected,
                .msg = HintFmt("no idea why"),
            }}}},
            BuildResult{
                .inner{BuildResult::Failure{{
                    .status = BuildResult::Failure::NotDeterministic,
                    .msg = HintFmt("no idea why"),
                    .isNonDeterministic = true,
                }}},
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
    WorkerProtoTest,
    buildResult_1_37,
    "build-result-1.37",
    (WorkerProto::Version{
        .number =
            {
                .major = 1,
                .minor = 37,
            },
    }),
    ({
        using namespace std::literals::chrono_literals;
        std::tuple<BuildResult, BuildResult, BuildResult> t{
            BuildResult{.inner{BuildResult::Failure{{
                .status = BuildResult::Failure::OutputRejected,
                .msg = HintFmt("no idea why"),
            }}}},
            BuildResult{
                .inner{BuildResult::Failure{{
                    .status = BuildResult::Failure::NotDeterministic,
                    .msg = HintFmt("no idea why"),
                    .isNonDeterministic = true,
                }}},
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

VERSIONED_CHARACTERIZATION_TEST(
    WorkerProtoTest,
    keyedBuildResult_1_29,
    "keyed-build-result-1.29",
    (WorkerProto::Version{
        .number =
            {
                .major = 1,
                .minor = 29,
            },
    }),
    ({
        using namespace std::literals::chrono_literals;
        std::tuple<KeyedBuildResult, KeyedBuildResult /*, KeyedBuildResult*/> t{
            KeyedBuildResult{
                BuildResult{.inner{KeyedBuildResult::Failure{{
                    .status = KeyedBuildResult::Failure::OutputRejected,
                    .msg = HintFmt("no idea why"),
                }}}},
                /* .path = */
                DerivedPath::Opaque{
                    StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-xxx"},
                },
            },
            KeyedBuildResult{
                BuildResult{
                    .inner{KeyedBuildResult::Failure{{
                        .status = KeyedBuildResult::Failure::NotDeterministic,
                        .msg = HintFmt("no idea why"),
                        .isNonDeterministic = true,
                    }}},
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
    (WorkerProto::Version{
        .number =
            {
                .major = 1,
                .minor = 15,
            },
    }),
    (std::tuple<UnkeyedValidPathInfo, UnkeyedValidPathInfo>{
        ({
            UnkeyedValidPathInfo info{
                std::string{defaultStoreDir},
                Hash::parseSRI("sha256-FePFYIlMuycIXPZbWi7LGEiMmZSX9FMbaQenWBzm1Sc="),
            };
            info.registrationTime = 23423;
            info.narSize = 34878;
            info;
        }),
        ({
            UnkeyedValidPathInfo info{
                std::string{defaultStoreDir},
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
    (WorkerProto::Version{
        .number =
            {
                .major = 1,
                .minor = 15,
            },
    }),
    (std::tuple<ValidPathInfo, ValidPathInfo>{
        ({
            ValidPathInfo info{
                StorePath{
                    "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar",
                },
                UnkeyedValidPathInfo{
                    std::string{defaultStoreDir},
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
                    std::string{defaultStoreDir},
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
    (WorkerProto::Version{
        .number =
            {
                .major = 1,
                .minor = 16,
            },
    }),
    (std::tuple<ValidPathInfo, ValidPathInfo, ValidPathInfo>{
        ({
            ValidPathInfo info{
                StorePath{
                    "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar",
                },
                UnkeyedValidPathInfo{
                    std::string{defaultStoreDir},
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
                    std::string{defaultStoreDir},
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
                    Signature{.keyName = "fake-sig-1", .sig = std::string(64, '\0')},
                    Signature{.keyName = "fake-sig-2", .sig = std::string(64, '\0')},
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
    (WorkerProto::Version{
        .number =
            {
                .major = 1,
                .minor = 30,
            },
    }),
    (std::tuple<WorkerProto::ClientHandshakeInfo>{
        {},
    }))

VERSIONED_CHARACTERIZATION_TEST_NO_JSON(
    WorkerProtoTest,
    clientHandshakeInfo_1_33,
    "client-handshake-info_1_33",
    (WorkerProto::Version{
        .number =
            {
                .major = 1,
                .minor = 33,
            },
    }),
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
    (WorkerProto::Version{
        .number =
            {
                .major = 1,
                .minor = 35,
            },
    }),
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
            clientResult = WorkerProto::BasicClientConnection::handshake(out, in, defaultVersion);
        });

        {
            FdSink out{toClient.writeSide.get()};
            FdSource in{toServer.readSide.get()};
            WorkerProto::BasicServerConnection::handshake(out, in, defaultVersion);
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

    WorkerProto::Version clientResult;

    auto clientThread = std::thread([&]() {
        FdSink out{toServer.writeSide.get()};
        FdSource in{toClient.readSide.get()};
        clientResult = WorkerProto::BasicClientConnection::handshake(
            out,
            in,
            WorkerProto::Version{
                .number = {.major = 1, .minor = 123},
                .features = {"bar", "aap", "mies", "xyzzy"},
            });
    });

    FdSink out{toClient.writeSide.get()};
    FdSource in{toServer.readSide.get()};
    auto daemonResult = WorkerProto::BasicServerConnection::handshake(
        out,
        in,
        WorkerProto::Version{
            .number = {.major = 1, .minor = 200},
            .features = {"foo", "bar", "xyzzy"},
        });

    clientThread.join();

    EXPECT_EQ(clientResult, daemonResult);
    EXPECT_EQ(
        clientResult,
        (WorkerProto::Version{
            .number =
                {
                    .major = 1,
                    .minor = 123,
                },
            .features = {"bar", "xyzzy"},
        }));
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
        auto clientResult = WorkerProto::BasicClientConnection::handshake(nullSink, in, defaultVersion);

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
                EXPECT_THROW(WorkerProto::BasicClientConnection::handshake(nullSink, in, defaultVersion), EndOfFile);
            } else {
                // Not sure why cannot keep on checking for `EndOfFile`.
                EXPECT_THROW(WorkerProto::BasicClientConnection::handshake(nullSink, in, defaultVersion), Error);
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
                EXPECT_THROW(WorkerProto::BasicClientConnection::handshake(nullSink, in, defaultVersion), Error);
            } else if (idx < 8 || idx >= 12) {
                // Number out of bounds
                EXPECT_THROW(
                    WorkerProto::BasicClientConnection::handshake(nullSink, in, defaultVersion), SerialisationError);
            } else {
                auto ver = WorkerProto::BasicClientConnection::handshake(nullSink, in, defaultVersion);
                // `std::min` of this and the other version saves us
                EXPECT_EQ(ver, defaultVersion);
            }
        }
    });
}

} // namespace nix
