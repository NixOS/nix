#include <regex>

#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include "common-protocol.hh"
#include "common-protocol-impl.hh"
#include "build-result.hh"
#include "tests/protocol.hh"
#include "tests/characterization.hh"

namespace nix {

const char commonProtoDir[] = "common-protocol";

class CommonProtoTest : public ProtoTest<CommonProto, commonProtoDir>
{
public:
    /**
     * Golden test for `T` reading
     */
    template<typename T>
    void readProtoTest(PathView testStem, const T & expected)
    {
        CharacterizationTest::readTest(testStem, [&](const auto & encoded) {
            T got = ({
                StringSource from { encoded };
                CommonProto::Serialise<T>::read(
                    *store,
                    CommonProto::ReadConn { .from = from });
            });

            ASSERT_EQ(got, expected);
        });
    }

    /**
     * Golden test for `T` write
     */
    template<typename T>
    void writeProtoTest(PathView testStem, const T & decoded)
    {
        CharacterizationTest::writeTest(testStem, [&]() -> std::string {
            StringSink to;
            CommonProto::Serialise<T>::write(
                *store,
                CommonProto::WriteConn { .to = to },
                decoded);
            return to.s;
        });
    }
};

#define CHARACTERIZATION_TEST(NAME, STEM, VALUE) \
    TEST_F(CommonProtoTest, NAME ## _read) { \
        readProtoTest(STEM, VALUE); \
    } \
    TEST_F(CommonProtoTest, NAME ## _write) { \
        writeProtoTest(STEM, VALUE); \
    }

CHARACTERIZATION_TEST(
    string,
    "string",
    (std::tuple<std::string, std::string, std::string, std::string, std::string> {
        "",
        "hi",
        "white rabbit",
        "大白兔",
        "oh no \0\0\0 what was that!",
    }))

CHARACTERIZATION_TEST(
    storePath,
    "store-path",
    (std::tuple<StorePath, StorePath> {
        StorePath { "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo" },
        StorePath { "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo-bar" },
    }))

CHARACTERIZATION_TEST(
    contentAddress,
    "content-address",
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

CHARACTERIZATION_TEST(
    drvOutput,
    "drv-output",
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

CHARACTERIZATION_TEST(
    realisation,
    "realisation",
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

CHARACTERIZATION_TEST(
    vector,
    "vector",
    (std::tuple<std::vector<std::string>, std::vector<std::string>, std::vector<std::string>, std::vector<std::vector<std::string>>> {
        { },
        { "" },
        { "", "foo", "bar" },
        { {}, { "" }, { "", "1", "2" } },
    }))

CHARACTERIZATION_TEST(
    set,
    "set",
    (std::tuple<std::set<std::string>, std::set<std::string>, std::set<std::string>, std::set<std::set<std::string>>> {
        { },
        { "" },
        { "", "foo", "bar" },
        { {}, { "" }, { "", "1", "2" } },
    }))

CHARACTERIZATION_TEST(
    optionalStorePath,
    "optional-store-path",
    (std::tuple<std::optional<StorePath>, std::optional<StorePath>> {
        std::nullopt,
        std::optional {
            StorePath { "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo-bar" },
        },
    }))

CHARACTERIZATION_TEST(
    optionalContentAddress,
    "optional-content-address",
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
