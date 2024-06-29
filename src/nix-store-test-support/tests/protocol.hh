#pragma once
///@file

#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include "tests/libstore.hh"
#include "tests/characterization.hh"

namespace nix {

template<class Proto, const char * protocolDir>
class ProtoTest : public CharacterizationTest, public LibStoreTest
{
    Path unitTestData = getUnitTestData() + "/" + protocolDir;

    Path goldenMaster(std::string_view testStem) const override {
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
    void readProtoTest(PathView testStem, typename Proto::Version version, T expected)
    {
        CharacterizationTest::readTest(testStem, [&](const auto & encoded) {
            T got = ({
                StringSource from { encoded };
                Proto::template Serialise<T>::read(
                    *LibStoreTest::store,
                    typename Proto::ReadConn {
                        .from = from,
                        .version = version,
                    });
            });

            ASSERT_EQ(got, expected);
        });
    }

    /**
     * Golden test for `T` write
     */
    template<typename T>
    void writeProtoTest(PathView testStem, typename Proto::Version version, const T & decoded)
    {
        CharacterizationTest::writeTest(testStem, [&]() {
            StringSink to;
            Proto::template Serialise<T>::write(
                *LibStoreTest::store,
                typename Proto::WriteConn {
                    .to = to,
                    .version = version,
                },
                decoded);
            return std::move(to.s);
        });
    }
};

#define VERSIONED_CHARACTERIZATION_TEST(FIXTURE, NAME, STEM, VERSION, VALUE) \
    TEST_F(FIXTURE, NAME ## _read) { \
        readProtoTest(STEM, VERSION, VALUE); \
    } \
    TEST_F(FIXTURE, NAME ## _write) { \
        writeProtoTest(STEM, VERSION, VALUE); \
    }

}
