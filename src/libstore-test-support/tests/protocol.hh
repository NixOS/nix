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
    std::filesystem::path unitTestData = getUnitTestData() / protocolDir;

    std::filesystem::path goldenMaster(std::string_view testStem) const override {
        return unitTestData / (std::string { testStem + ".bin" });
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
                std::set<std::string> features;
                Proto::template Serialise<T>::read(
                    *LibStoreTest::store,
                    typename Proto::ReadConn {
                        .from = from,
                        .version = version,
                        .features = features,
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
            std::set<std::string> features;
            Proto::template Serialise<T>::write(
                *LibStoreTest::store,
                typename Proto::WriteConn {
                    .to = to,
                    .version = version,
                    .features = features,
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
