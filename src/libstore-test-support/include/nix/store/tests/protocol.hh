#pragma once
///@file

#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include "nix/store/tests/libstore.hh"
#include "nix/util/tests/characterization.hh"

namespace nix {

template<class Proto, const char * protocolDir>
class ProtoTest : public CharacterizationTest
{
    std::filesystem::path unitTestData = getUnitTestData() / protocolDir;

    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / (std::string{testStem + ".bin"});
    }

public:
    Path storeDir = "/nix/store";
    StoreDirConfig store{storeDir};
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
                StringSource from{encoded};
                Proto::template Serialise<T>::read(
                    this->store,
                    typename Proto::ReadConn{
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
                this->store,
                typename Proto::WriteConn{
                    .to = to,
                    .version = version,
                },
                decoded);
            return std::move(to.s);
        });
    }
};

#define VERSIONED_CHARACTERIZATION_TEST(FIXTURE, NAME, STEM, VERSION, VALUE) \
    TEST_F(FIXTURE, NAME##_read)                                             \
    {                                                                        \
        readProtoTest(STEM, VERSION, VALUE);                                 \
    }                                                                        \
    TEST_F(FIXTURE, NAME##_write)                                            \
    {                                                                        \
        writeProtoTest(STEM, VERSION, VALUE);                                \
    }

} // namespace nix
