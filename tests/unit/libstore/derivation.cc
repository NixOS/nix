#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include "experimental-features.hh"
#include "derivations.hh"

#include "tests/libstore.hh"
#include "tests/characterization.hh"

namespace nix {

using nlohmann::json;

class DerivationTest : public CharacterizationTest, public LibStoreTest
{
    Path unitTestData = getUnitTestData() + "/derivation";

public:
    Path goldenMaster(std::string_view testStem) const override {
        return unitTestData + "/" + testStem;
    }

    /**
     * We set these in tests rather than the regular globals so we don't have
     * to worry about race conditions if the tests run concurrently.
     */
    ExperimentalFeatureSettings mockXpSettings;
};

class CaDerivationTest : public DerivationTest
{
    void SetUp() override
    {
        mockXpSettings.set("experimental-features", "ca-derivations");
    }
};

class DynDerivationTest : public DerivationTest
{
    void SetUp() override
    {
        mockXpSettings.set("experimental-features", "dynamic-derivations ca-derivations");
    }
};

class ImpureDerivationTest : public DerivationTest
{
    void SetUp() override
    {
        mockXpSettings.set("experimental-features", "impure-derivations");
    }
};

TEST_F(DerivationTest, BadATerm_version) {
    ASSERT_THROW(
        parseDerivation(
            *store,
            readFile(goldenMaster("bad-version.drv")),
            "whatever",
            mockXpSettings),
        FormatError);
}

TEST_F(DynDerivationTest, BadATerm_oldVersionDynDeps) {
    ASSERT_THROW(
        parseDerivation(
            *store,
            readFile(goldenMaster("bad-old-version-dyn-deps.drv")),
            "dyn-dep-derivation",
            mockXpSettings),
        FormatError);
}

#define TEST_JSON(FIXTURE, NAME, VAL, DRV_NAME, OUTPUT_NAME)              \
    TEST_F(FIXTURE, DerivationOutput_ ## NAME ## _from_json) {            \
        readTest("output-" #NAME ".json", [&](const auto & encoded_) {    \
            auto encoded = json::parse(encoded_);                         \
            DerivationOutput got = DerivationOutput::fromJSON(            \
                *store,                                                   \
                DRV_NAME,                                                 \
                OUTPUT_NAME,                                              \
                encoded,                                                  \
                mockXpSettings);                                          \
            DerivationOutput expected { VAL };                            \
            ASSERT_EQ(got, expected);                                     \
        });                                                               \
    }                                                                     \
                                                                          \
    TEST_F(FIXTURE, DerivationOutput_ ## NAME ## _to_json) {              \
        writeTest("output-" #NAME ".json", [&]() -> json {                \
            return DerivationOutput { (VAL) }.toJSON(                     \
                *store,                                                   \
                (DRV_NAME),                                               \
                (OUTPUT_NAME));                                           \
        }, [](const auto & file) {                                        \
            return json::parse(readFile(file));                           \
        }, [](const auto & file, const auto & got) {                      \
            return writeFile(file, got.dump(2) + "\n");                   \
        });                                                               \
    }

TEST_JSON(DerivationTest, inputAddressed,
    (DerivationOutput::InputAddressed {
        .path = store->parseStorePath("/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-drv-name-output-name"),
    }),
    "drv-name", "output-name")

TEST_JSON(DerivationTest, caFixedFlat,
    (DerivationOutput::CAFixed {
        .ca = {
            .method = FileIngestionMethod::Flat,
            .hash = Hash::parseAnyPrefixed("sha256-iUUXyRY8iW7DGirb0zwGgf1fRbLA7wimTJKgP7l/OQ8="),
        },
    }),
    "drv-name", "output-name")

TEST_JSON(DerivationTest, caFixedNAR,
    (DerivationOutput::CAFixed {
        .ca = {
            .method = FileIngestionMethod::Recursive,
            .hash = Hash::parseAnyPrefixed("sha256-iUUXyRY8iW7DGirb0zwGgf1fRbLA7wimTJKgP7l/OQ8="),
        },
    }),
    "drv-name", "output-name")

TEST_JSON(DynDerivationTest, caFixedText,
    (DerivationOutput::CAFixed {
        .ca = {
            .hash = Hash::parseAnyPrefixed("sha256-iUUXyRY8iW7DGirb0zwGgf1fRbLA7wimTJKgP7l/OQ8="),
        },
    }),
    "drv-name", "output-name")

TEST_JSON(CaDerivationTest, caFloating,
    (DerivationOutput::CAFloating {
        .method = FileIngestionMethod::Recursive,
        .hashAlgo = HashAlgorithm::SHA256,
    }),
    "drv-name", "output-name")

TEST_JSON(DerivationTest, deferred,
    DerivationOutput::Deferred { },
    "drv-name", "output-name")

TEST_JSON(ImpureDerivationTest, impure,
    (DerivationOutput::Impure {
        .method = FileIngestionMethod::Recursive,
        .hashAlgo = HashAlgorithm::SHA256,
    }),
    "drv-name", "output-name")

#undef TEST_JSON

#define TEST_JSON(FIXTURE, NAME, VAL)                                     \
    TEST_F(FIXTURE, Derivation_ ## NAME ## _from_json) {                  \
        readTest(#NAME ".json", [&](const auto & encoded_) {              \
            auto encoded = json::parse(encoded_);                         \
            Derivation expected { VAL };                                  \
            Derivation got = Derivation::fromJSON(                        \
                *store,                                                   \
                encoded,                                                  \
                mockXpSettings);                                          \
            ASSERT_EQ(got, expected);                                     \
        });                                                               \
    }                                                                     \
                                                                          \
    TEST_F(FIXTURE, Derivation_ ## NAME ## _to_json) {                    \
        writeTest(#NAME ".json", [&]() -> json {                          \
            return Derivation { VAL }.toJSON(*store);                     \
        }, [](const auto & file) {                                        \
            return json::parse(readFile(file));                           \
        }, [](const auto & file, const auto & got) {                      \
            return writeFile(file, got.dump(2) + "\n");                   \
        });                                                               \
    }

#define TEST_ATERM(FIXTURE, NAME, VAL, DRV_NAME)                          \
    TEST_F(FIXTURE, Derivation_ ## NAME ## _from_aterm) {                 \
        readTest(#NAME ".drv", [&](auto encoded) {                        \
            Derivation expected { VAL };                                  \
            auto got = parseDerivation(                                   \
                *store,                                                   \
                std::move(encoded),                                       \
                DRV_NAME,                                                 \
                mockXpSettings);                                          \
            ASSERT_EQ(got.toJSON(*store), expected.toJSON(*store)) ;      \
            ASSERT_EQ(got, expected);                                     \
        });                                                               \
    }                                                                     \
                                                                          \
    TEST_F(FIXTURE, Derivation_ ## NAME ## _to_aterm) {                   \
        writeTest(#NAME ".drv", [&]() -> std::string {                    \
            return (VAL).unparse(*store, false);                          \
        });                                                               \
    }

Derivation makeSimpleDrv(const Store & store) {
    Derivation drv;
    drv.name = "simple-derivation";
    drv.inputSrcs = {
        store.parseStorePath("/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep1"),
    };
    drv.inputDrvs = {
        .map = {
            {
                store.parseStorePath("/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep2.drv"),
                {
                    .value = {
                        "cat",
                        "dog",
                    },
                },
            },
        },
    };
    drv.platform = "wasm-sel4";
    drv.builder = "foo";
    drv.args = {
        "bar",
        "baz",
    };
    drv.env = {
        {
            "BIG_BAD",
            "WOLF",
        },
    };
    return drv;
}

TEST_JSON(DerivationTest, simple, makeSimpleDrv(*store))

TEST_ATERM(DerivationTest, simple,
    makeSimpleDrv(*store),
    "simple-derivation")

Derivation makeDynDepDerivation(const Store & store) {
    Derivation drv;
    drv.name = "dyn-dep-derivation";
    drv.inputSrcs = {
        store.parseStorePath("/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep1"),
    };
    drv.inputDrvs = {
        .map = {
            {
                store.parseStorePath("/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep2.drv"),
                DerivedPathMap<StringSet>::ChildNode {
                    .value = {
                        "cat",
                        "dog",
                    },
                    .childMap = {
                        {
                            "cat",
                            DerivedPathMap<StringSet>::ChildNode {
                                .value = {
                                    "kitten",
                                },
                            },
                        },
                        {
                            "goose",
                            DerivedPathMap<StringSet>::ChildNode {
                                .value = {
                                    "gosling",
                                },
                            },
                        },
                    },
                },
            },
        },
    };
    drv.platform = "wasm-sel4";
    drv.builder = "foo";
    drv.args = {
        "bar",
        "baz",
    };
    drv.env = {
        {
            "BIG_BAD",
            "WOLF",
        },
    };
    return drv;
}

TEST_JSON(DynDerivationTest, dynDerivationDeps, makeDynDepDerivation(*store))

TEST_ATERM(DynDerivationTest, dynDerivationDeps,
    makeDynDepDerivation(*store),
    "dyn-dep-derivation")

#undef TEST_JSON
#undef TEST_ATERM

}
