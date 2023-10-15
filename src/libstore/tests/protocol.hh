#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include "tests/libstore.hh"
#include "tests/characterization.hh"

namespace nix {

template<class Proto, const char * protocolDir>
class ProtoTest : public LibStoreTest
{
    /**
     * Read this as simply `using S = Inner::Serialise;`.
     *
     * See `LengthPrefixedProtoHelper::S` for the same trick, and its
     * rationale.
     */
    template<typename U> using S = typename Proto::template Serialise<U>;

public:
    Path unitTestData = getUnitTestData() + "/libstore/" + protocolDir;

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
                S<T>::read(
                    *store,
                    typename Proto::ReadConn { .from = from });
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
        Proto::write(
            *store,
            typename Proto::WriteConn { .to = to },
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

#define CHARACTERIZATION_TEST(FIXTURE, NAME, STEM, VALUE) \
    TEST_F(FIXTURE, NAME ## _read) { \
        readTest(STEM, VALUE); \
    } \
    TEST_F(FIXTURE, NAME ## _write) { \
        writeTest(STEM, VALUE); \
    }

}
