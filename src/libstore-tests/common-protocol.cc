#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include "nix/util/json-utils.hh"
#include "nix/store/common-protocol.hh"
#include "nix/store/common-protocol-impl.hh"
#include "nix/store/tests/protocol.hh"
#include "nix/util/tests/characterization.hh"

namespace nix {

const char commonProtoDir[] = "common-protocol";

class CommonProtoTest : public ProtoTest<CommonProto, commonProtoDir>
{
public:
    /**
     * Golden test for `T` reading
     */
    template<typename T>
    void readProtoTest(std::string_view testStem, const T & expected)
    {
        CharacterizationTest::readTest(std::string{testStem + ".bin"}, [&](const auto & encoded) {
            T got = ({
                StringSource from{encoded};
                CommonProto::Serialise<T>::read(store, CommonProto::ReadConn{.from = from});
            });

            ASSERT_EQ(got, expected);
        });
    }

    /**
     * Golden test for `T` write
     */
    template<typename T>
    void writeProtoTest(std::string_view testStem, const T & decoded)
    {
        CharacterizationTest::writeTest(std::string{testStem + ".bin"}, [&]() -> std::string {
            StringSink to;
            CommonProto::Serialise<T>::write(store, CommonProto::WriteConn{.to = to}, decoded);
            return to.s;
        });
    }
};

#define READ_CHARACTERIZATION_TEST(NAME, STEM, VALUE) \
    TEST_F(CommonProtoTest, NAME##_read)              \
    {                                                 \
        readProtoTest(STEM, VALUE);                   \
    }                                                 \
    TEST_F(CommonProtoTest, NAME##_json_read)         \
    {                                                 \
        readJsonTest(STEM, VALUE);                    \
    }

#define WRITE_CHARACTERIZATION_TEST(NAME, STEM, VALUE) \
    TEST_F(CommonProtoTest, NAME##_write)              \
    {                                                  \
        writeProtoTest(STEM, VALUE);                   \
    }                                                  \
    TEST_F(CommonProtoTest, NAME##_json_write)         \
    {                                                  \
        writeJsonTest(STEM, VALUE);                    \
    }

#define CHARACTERIZATION_TEST(NAME, STEM, VALUE)  \
    READ_CHARACTERIZATION_TEST(NAME, STEM, VALUE) \
    WRITE_CHARACTERIZATION_TEST(NAME, STEM, VALUE)

CHARACTERIZATION_TEST(
    string,
    "string",
    (std::tuple<std::string, std::string, std::string, std::string, std::string>{
        "",
        "hi",
        "white rabbit",
        "大白兔",
        "oh no \0\0\0 what was that!",
    }))

CHARACTERIZATION_TEST(
    storePath,
    "store-path",
    (std::tuple<StorePath, StorePath>{
        StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo"},
        StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo-bar"},
    }))

CHARACTERIZATION_TEST(
    contentAddress,
    "content-address",
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

CHARACTERIZATION_TEST(
    vector,
    "vector",
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

CHARACTERIZATION_TEST(
    set,
    "set",
    (std::tuple<StringSet, StringSet, StringSet, std::set<StringSet>>{
        {},
        {""},
        {"", "foo", "bar"},
        {{}, {""}, {"", "1", "2"}},
    }))

CHARACTERIZATION_TEST(
    optionalStorePath,
    "optional-store-path",
    (std::tuple<std::optional<StorePath>, std::optional<StorePath>>{
        std::nullopt,
        std::optional{
            StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo-bar"},
        },
    }))

CHARACTERIZATION_TEST(
    optionalContentAddress,
    "optional-content-address",
    (std::tuple<std::optional<ContentAddress>, std::optional<ContentAddress>>{
        std::nullopt,
        std::optional{
            ContentAddress{
                .method = ContentAddressMethod::Raw::Flat,
                .hash = hashString(HashAlgorithm::SHA1, "blob blob..."),
            },
        },
    }))

class CommonProtoStorePathTest : public ::testing::Test
{
    std::string storeDir = "/nix/store";

protected:
    StoreDirConfig store{storeDir};

    std::string makeBasename(std::size_t nameLength) const
    {
        return std::string(StorePath::dummy.hashPart()) + "-" + std::string(nameLength, 'x');
    }
};

TEST_F(CommonProtoStorePathTest, maximum_length)
{
    auto baseName = makeBasename(StorePath::MaxNameLen);
    auto storePath = store.storeDir + "/" + baseName;
    StringSource from{[&] {
        StringSink sink;
        writeString(storePath, sink);
        return sink.s;
    }()};
    EXPECT_EQ(CommonProto::Serialise<StorePath>::read(store, {.from = from}), StorePath(baseName));
}

TEST_F(CommonProtoStorePathTest, too_long)
{
    std::string storePath = store.storeDir + "/" + std::string(StorePath::dummy.hashPart()) + "-"
                            + std::string(StorePath::MaxNameLen + 1, 'x');
    StringSource from{[&] {
        StringSink sink;
        writeString(storePath, sink);
        return sink.s;
    }()};
    EXPECT_THROW(CommonProto::Serialise<StorePath>::read(store, {.from = from}), SerialisationError);
}

TEST_F(CommonProtoStorePathTest, wrong_store_dir)
{
    std::string storePath = "/gnu/store/" + makeBasename(StorePath::MaxNameLen);
    StringSource from{[&] {
        StringSink sink;
        writeString(storePath, sink);
        return sink.s;
    }()};
    EXPECT_THROW(CommonProto::Serialise<StorePath>::read(store, {.from = from}), BadStorePath);
}

TEST_F(CommonProtoStorePathTest, no_dot_dots)
{
    /* Everything immediately following the `/` is interpreted as a CanonPath, so `/` or `..`
       has no special meaning. */
    std::string storePath = store.storeDir + "/../" + makeBasename(8);
    StringSource from{[&] {
        StringSink sink;
        writeString(storePath, sink);
        return sink.s;
    }()};
    EXPECT_THROW(CommonProto::Serialise<StorePath>::read(store, {.from = from}), BadStorePath);
}

} // namespace nix
