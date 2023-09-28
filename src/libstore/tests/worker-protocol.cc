#include <regex>

#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include "worker-protocol.hh"
#include "worker-protocol-impl.hh"
#include "derived-path.hh"
#include "tests/libstore.hh"

namespace nix {

class WorkerProtoTest : public LibStoreTest
{
public:
    Path unitTestData = getEnv("_NIX_TEST_UNIT_DATA").value() + "/libstore/worker-protocol";

    bool testAccept() {
        return getEnv("_NIX_TEST_ACCEPT") == "1";
    }

    Path goldenMaster(std::string_view testStem) {
        return unitTestData + "/" + testStem + ".bin";
    }

    /**
     * Golden test for `T` reading
     */
    template<typename T>
    void readTest(PathView testStem, T value)
    {
        if (testAccept())
        {
            GTEST_SKIP() << "Cannot read golden master because another test is also updating it";
        }
        else
        {
            auto expected = readFile(goldenMaster(testStem));

            T got = ({
                StringSource from { expected };
                WorkerProto::Serialise<T>::read(
                    *store,
                    WorkerProto::ReadConn { .from = from });
            });

            ASSERT_EQ(got, value);
        }
    }

    /**
     * Golden test for `T` write
     */
    template<typename T>
    void writeTest(PathView testStem, const T & value)
    {
        auto file = goldenMaster(testStem);

        StringSink to;
        WorkerProto::write(
            *store,
            WorkerProto::WriteConn { .to = to },
            value);

        if (testAccept())
        {
            createDirs(dirOf(file));
            writeFile(file, to.s);
            GTEST_SKIP() << "Updating golden master";
        }
        else
        {
            auto expected = readFile(file);
            ASSERT_EQ(to.s, expected);
        }
    }
};

#define CHARACTERIZATION_TEST(NAME, STEM, VALUE)  \
    TEST_F(WorkerProtoTest, NAME ## _read) {   \
        readTest(STEM, VALUE);                 \
    }                                          \
    TEST_F(WorkerProtoTest, NAME ## _write) {  \
        writeTest(STEM, VALUE);                \
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

CHARACTERIZATION_TEST(
    derivedPath,
    "derived-path",
    (std::tuple<DerivedPath, DerivedPath> {
        DerivedPath::Opaque {
            .path = StorePath { "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo" },
        },
        DerivedPath::Built {
            .drvPath = makeConstantStorePathRef(StorePath {
                "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar.drv",
            }),
            .outputs = OutputsSpec::Names { "x", "y" },
        },
    }))

}
