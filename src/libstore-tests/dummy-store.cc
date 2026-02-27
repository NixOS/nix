#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "nix/util/memory-source-accessor.hh"
#include "nix/store/dummy-store-impl.hh"
#include "nix/store/globals.hh"
#include "nix/store/realisation.hh"

#include "nix/util/tests/json-characterization.hh"

namespace nix {

class DummyStoreTest : public virtual CharacterizationTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "dummy-store";

public:

    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / testStem;
    }

    static void SetUpTestSuite()
    {
        initLibStore(false);
    }
};

TEST(DummyStore, realisation_read)
{
    initLibStore(/*loadConfig=*/false);

    auto store = [] {
        auto cfg = make_ref<DummyStoreConfig>(StoreReference::Params{});
        cfg->readOnly = false;
        return cfg->openDummyStore();
    }();

    auto drvHash = Hash::parseExplicitFormatUnprefixed(
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", HashAlgorithm::SHA256, HashFormat::Base16);

    auto outputName = "foo";

    EXPECT_EQ(store->queryRealisation({drvHash, outputName}), nullptr);

    UnkeyedRealisation value{
        .outPath = StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo.drv"},
    };

    store->buildTrace.insert({drvHash, {{outputName, value}}});

    auto value2 = store->queryRealisation({drvHash, outputName});

    ASSERT_TRUE(value2);
    EXPECT_EQ(*value2, value);
}

/* ----------------------------------------------------------------------------
 * JSON
 * --------------------------------------------------------------------------*/

using nlohmann::json;

struct DummyStoreJsonTest : DummyStoreTest,
                            JsonCharacterizationTest<ref<DummyStore>>,
                            ::testing::WithParamInterface<std::pair<std::string_view, ref<DummyStore>>>
{};

TEST_P(DummyStoreJsonTest, from_json)
{
    auto & [name, expected] = GetParam();
    using namespace nlohmann;
    /* Cannot use `readJsonTest` because need to dereference the stores
       for equality. */
    readTest(std::string{name} + ".json", [&](const auto & encodedRaw) {
        auto encoded = json::parse(encodedRaw);
        ref<DummyStore> decoded = adl_serializer<ref<DummyStore>>::from_json(encoded);
        ASSERT_EQ(*decoded, *expected);
    });
}

TEST_P(DummyStoreJsonTest, to_json)
{
    auto & [name, value] = GetParam();
    writeJsonTest(name, value);
}

INSTANTIATE_TEST_SUITE_P(DummyStoreJSON, DummyStoreJsonTest, [] {
    initLibStore(false);
    auto writeCfg = make_ref<DummyStore::Config>(DummyStore::Config::Params{});
    writeCfg->readOnly = false;
    return ::testing::Values(
        std::pair{
            "empty",
            make_ref<DummyStore::Config>(DummyStore::Config::Params{})->openDummyStore(),
        },
        std::pair{
            "one-flat-file",
            [&] {
                auto store = writeCfg->openDummyStore();
                store->addToStore(
                    "my-file",
                    SourcePath{
                        [] {
                            auto sc = make_ref<MemorySourceAccessor>();
                            sc->root = MemorySourceAccessor::File{MemorySourceAccessor::File::Regular{
                                .executable = false,
                                .contents = "asdf",
                            }};
                            return sc;
                        }(),
                    },
                    ContentAddressMethod::Raw::NixArchive,
                    HashAlgorithm::SHA256);
                return store;
            }(),
        },
        std::pair{
            "one-derivation",
            [&] {
                auto store = writeCfg->openDummyStore();
                Derivation drv;
                drv.name = "foo";
                store->writeDerivation(drv);
                return store;
            }(),
        },
        std::pair{
            "one-realisation",
            [&] {
                auto store = writeCfg->openDummyStore();
                store->buildTrace.insert_or_assign(
                    Hash::parseExplicitFormatUnprefixed(
                        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                        HashAlgorithm::SHA256,
                        HashFormat::Base16),
                    std::map<std::string, UnkeyedRealisation>{
                        {
                            "out",
                            UnkeyedRealisation{
                                .outPath = StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo"},
                            },
                        },
                    });
                return store;
            }(),
        });
}());

} // namespace nix
