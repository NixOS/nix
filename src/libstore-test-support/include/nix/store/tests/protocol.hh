#pragma once
///@file

#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include "nix/store/tests/libstore.hh"
#include "nix/util/tests/characterization.hh"
#include "nix/util/tests/json-characterization.hh"

namespace nix {

template<class Proto, const char * protocolDir>
class ProtoTest : public CharacterizationTest
{
    std::filesystem::path unitTestData = getUnitTestData() / protocolDir;

    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / testStem;
    }

public:
    Path storeDir = "/nix/store";
    StoreDirConfig store{storeDir};

    /**
     * Golden test for `T` JSON reading
     */
    template<typename T>
    void readJsonTest(PathView testStem, const T & expected)
    {
        nix::readJsonTest(*this, testStem, expected);
    }

    /**
     * Golden test for `T` JSON write
     */
    template<typename T>
    void writeJsonTest(PathView testStem, const T & decoded)
    {
        nix::writeJsonTest(*this, testStem, decoded);
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
        CharacterizationTest::readTest(std::string{testStem + ".bin"}, [&](const auto & encoded) {
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
        CharacterizationTest::writeTest(std::string{testStem + ".bin"}, [&]() {
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

#define VERSIONED_CHARACTERIZATION_TEST_NO_JSON(FIXTURE, NAME, STEM, VERSION, VALUE) \
    TEST_F(FIXTURE, NAME##_read)                                                     \
    {                                                                                \
        readProtoTest(STEM, VERSION, VALUE);                                         \
    }                                                                                \
    TEST_F(FIXTURE, NAME##_write)                                                    \
    {                                                                                \
        writeProtoTest(STEM, VERSION, VALUE);                                        \
    }

#define VERSIONED_CHARACTERIZATION_TEST(FIXTURE, NAME, STEM, VERSION, VALUE)     \
    VERSIONED_CHARACTERIZATION_TEST_NO_JSON(FIXTURE, NAME, STEM, VERSION, VALUE) \
    TEST_F(FIXTURE, NAME##_json_read)                                            \
    {                                                                            \
        readJsonTest(STEM, VALUE);                                               \
    }                                                                            \
    TEST_F(FIXTURE, NAME##_json_write)                                           \
    {                                                                            \
        writeJsonTest(STEM, VALUE);                                              \
    }

} // namespace nix
