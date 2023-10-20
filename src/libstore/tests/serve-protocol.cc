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
    ServeProto::Version defaultVersion = 1 << 8 | 0;
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
            .hash = hashString(HashType::htSHA256, "Derive(...)"),
        },
        ContentAddress {
            .method = FileIngestionMethod::Flat,
            .hash = hashString(HashType::htSHA1, "blob blob..."),
        },
        ContentAddress {
            .method = FileIngestionMethod::Recursive,
            .hash = hashString(HashType::htSHA256, "(...)"),
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
                .hash = hashString(HashType::htSHA1, "blob blob..."),
            },
        },
    }))

}
