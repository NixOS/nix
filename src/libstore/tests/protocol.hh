#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include "tests/libstore.hh"
#include "tests/characterization.hh"

namespace nix {

template<class Proto, const char * protocolDir>
class ProtoTest : public LibStoreTest
{
protected:
    Path unitTestData = getUnitTestData() + "/libstore/" + protocolDir;

    Path goldenMaster(std::string_view testStem) {
        return unitTestData + "/" + testStem + ".bin";
    }
};

template<class Proto, const char * protocolDir>
class VersionedProtoTest : public ProtoTest<Proto, protocolDir>
{
public:
    /**
     * Golden test for `T` reading
     */
    template<typename T>
    void readTest(PathView testStem, typename Proto::Version version, T value)
    {
        if (testAccept())
        {
            GTEST_SKIP() << "Cannot read golden master because another test is also updating it";
        }
        else
        {
            auto expected = readFile(ProtoTest<Proto, protocolDir>::goldenMaster(testStem));

            T got = ({
                StringSource from { expected };
                Proto::template Serialise<T>::read(
                    *LibStoreTest::store,
                    typename Proto::ReadConn {
                        .from = from,
                        .version = version,
                    });
            });

            ASSERT_EQ(got, value);
        }
    }

    /**
     * Golden test for `T` write
     */
    template<typename T>
    void writeTest(PathView testStem, typename Proto::Version version, const T & value)
    {
        auto file = ProtoTest<Proto, protocolDir>::goldenMaster(testStem);

        StringSink to;
        Proto::write(
            *LibStoreTest::store,
            typename Proto::WriteConn {
                .to = to,
                .version = version,
            },
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

#define VERSIONED_CHARACTERIZATION_TEST(FIXTURE, NAME, STEM, VERSION, VALUE) \
    TEST_F(FIXTURE, NAME ## _read) { \
        readTest(STEM, VERSION, VALUE); \
    } \
    TEST_F(FIXTURE, NAME ## _write) { \
        writeTest(STEM, VERSION, VALUE); \
    }

}
