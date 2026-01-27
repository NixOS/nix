#include <string_view>

#include "nix/util/nar-accessor.hh"
#include "nix/util/tests/json-characterization.hh"

namespace nix {

// Forward declaration from memory-source-accessor.cc
namespace memory_source_accessor {
ref<MemorySourceAccessor> exampleComplex();
}

/* ----------------------------------------------------------------------------
 * JSON
 * --------------------------------------------------------------------------*/

class NarListingTest : public virtual CharacterizationTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "nar-listing";

public:

    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / testStem;
    }
};

using nlohmann::json;

struct NarListingJsonTest : NarListingTest,
                            JsonCharacterizationTest<NarListing>,
                            ::testing::WithParamInterface<std::pair<std::string_view, NarListing>>
{};

TEST_P(NarListingJsonTest, from_json)
{
    auto & [name, expected] = GetParam();
    readJsonTest(name, expected);
}

TEST_P(NarListingJsonTest, to_json)
{
    auto & [name, value] = GetParam();
    writeJsonTest(name, value);
}

INSTANTIATE_TEST_SUITE_P(
    NarListingJSON,
    NarListingJsonTest,
    ::testing::Values(
        std::pair{
            "deep",
            listNarDeep(*memory_source_accessor::exampleComplex(), CanonPath::root),
        }));

struct ShallowNarListingJsonTest : NarListingTest,
                                   JsonCharacterizationTest<ShallowNarListing>,
                                   ::testing::WithParamInterface<std::pair<std::string_view, ShallowNarListing>>
{};

TEST_P(ShallowNarListingJsonTest, from_json)
{
    auto & [name, expected] = GetParam();
    readJsonTest(name, expected);
}

TEST_P(ShallowNarListingJsonTest, to_json)
{
    auto & [name, value] = GetParam();
    writeJsonTest(name, value);
}

INSTANTIATE_TEST_SUITE_P(
    ShallowNarListingJSON,
    ShallowNarListingJsonTest,
    ::testing::Values(
        std::pair{
            "shallow",
            listNarShallow(*memory_source_accessor::exampleComplex(), CanonPath::root),
        }));

} // namespace nix
