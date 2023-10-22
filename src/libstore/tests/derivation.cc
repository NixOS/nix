#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include "experimental-features.hh"
#include "derivations.hh"

#include "tests/libstore.hh"

namespace nix {

class DerivationTest : public LibStoreTest
{
public:
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
            R"(DrvWithVersion("invalid-version",[],[("/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep2.drv",["cat","dog"])],["/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep1"],"wasm-sel4","foo",["bar","baz"],[("BIG_BAD","WOLF")]))",
            "whatever",
            mockXpSettings),
        FormatError);
}

TEST_F(DynDerivationTest, BadATerm_oldVersionDynDeps) {
    ASSERT_THROW(
        parseDerivation(
            *store,
            R"(Derive([],[("/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep2.drv",(["cat","dog"],[("cat",["kitten"]),("goose",["gosling"])]))],["/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep1"],"wasm-sel4","foo",["bar","baz"],[("BIG_BAD","WOLF")]))",
            "dyn-dep-derivation",
            mockXpSettings),
        FormatError);
}

#define TEST_JSON(FIXTURE, NAME, STR, VAL, DRV_NAME, OUTPUT_NAME) \
    TEST_F(FIXTURE, DerivationOutput_ ## NAME ## _to_json) {    \
        using nlohmann::literals::operator "" _json;           \
        ASSERT_EQ(                                             \
            STR ## _json,                                      \
            (DerivationOutput { VAL }).toJSON(                 \
                *store,                                        \
                DRV_NAME,                                      \
                OUTPUT_NAME));                                 \
    }                                                          \
                                                               \
    TEST_F(FIXTURE, DerivationOutput_ ## NAME ## _from_json) {  \
        using nlohmann::literals::operator "" _json;           \
        ASSERT_EQ(                                             \
            DerivationOutput { VAL },                          \
            DerivationOutput::fromJSON(                        \
                *store,                                        \
                DRV_NAME,                                      \
                OUTPUT_NAME,                                   \
                STR ## _json,                                  \
                mockXpSettings));                              \
    }

TEST_JSON(DerivationTest, inputAddressed,
    R"({
        "path": "/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-drv-name-output-name"
    })",
    (DerivationOutput::InputAddressed {
        .path = store->parseStorePath("/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-drv-name-output-name"),
    }),
    "drv-name", "output-name")

TEST_JSON(DerivationTest, caFixedFlat,
    R"({
        "hashAlgo": "sha256",
        "hash": "894517c9163c896ec31a2adbd33c0681fd5f45b2c0ef08a64c92a03fb97f390f",
        "path": "/nix/store/rhcg9h16sqvlbpsa6dqm57sbr2al6nzg-drv-name-output-name"
    })",
    (DerivationOutput::CAFixed {
        .ca = {
            .method = FileIngestionMethod::Flat,
            .hash = Hash::parseAnyPrefixed("sha256-iUUXyRY8iW7DGirb0zwGgf1fRbLA7wimTJKgP7l/OQ8="),
        },
    }),
    "drv-name", "output-name")

TEST_JSON(DerivationTest, caFixedNAR,
    R"({
        "hashAlgo": "r:sha256",
        "hash": "894517c9163c896ec31a2adbd33c0681fd5f45b2c0ef08a64c92a03fb97f390f",
        "path": "/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-drv-name-output-name"
    })",
    (DerivationOutput::CAFixed {
        .ca = {
            .method = FileIngestionMethod::Recursive,
            .hash = Hash::parseAnyPrefixed("sha256-iUUXyRY8iW7DGirb0zwGgf1fRbLA7wimTJKgP7l/OQ8="),
        },
    }),
    "drv-name", "output-name")

TEST_JSON(DynDerivationTest, caFixedText,
    R"({
        "hashAlgo": "text:sha256",
        "hash": "894517c9163c896ec31a2adbd33c0681fd5f45b2c0ef08a64c92a03fb97f390f",
        "path": "/nix/store/6s1zwabh956jvhv4w9xcdb5jiyanyxg1-drv-name-output-name"
    })",
    (DerivationOutput::CAFixed {
        .ca = {
            .hash = Hash::parseAnyPrefixed("sha256-iUUXyRY8iW7DGirb0zwGgf1fRbLA7wimTJKgP7l/OQ8="),
        },
    }),
    "drv-name", "output-name")

TEST_JSON(CaDerivationTest, caFloating,
    R"({
        "hashAlgo": "r:sha256"
    })",
    (DerivationOutput::CAFloating {
        .method = FileIngestionMethod::Recursive,
        .hashType = htSHA256,
    }),
    "drv-name", "output-name")

TEST_JSON(DerivationTest, deferred,
    R"({ })",
    DerivationOutput::Deferred { },
    "drv-name", "output-name")

TEST_JSON(ImpureDerivationTest, impure,
    R"({
        "hashAlgo": "r:sha256",
        "impure": true
    })",
    (DerivationOutput::Impure {
        .method = FileIngestionMethod::Recursive,
        .hashType = htSHA256,
    }),
    "drv-name", "output-name")

#undef TEST_JSON

#define TEST_JSON(FIXTURE, NAME, STR, VAL)                       \
    TEST_F(FIXTURE, Derivation_ ## NAME ## _to_json) {           \
        using nlohmann::literals::operator "" _json;             \
        ASSERT_EQ(                                               \
            STR ## _json,                                        \
            (VAL).toJSON(*store));                               \
    }                                                            \
                                                                 \
    TEST_F(FIXTURE, Derivation_ ## NAME ## _from_json) {         \
        using nlohmann::literals::operator "" _json;             \
        ASSERT_EQ(                                               \
            (VAL),                                               \
            Derivation::fromJSON(                                \
                *store,                                          \
                STR ## _json,                                    \
                mockXpSettings));                                \
    }

#define TEST_ATERM(FIXTURE, NAME, STR, VAL, DRV_NAME)            \
    TEST_F(FIXTURE, Derivation_ ## NAME ## _to_aterm) {          \
        ASSERT_EQ(                                               \
            STR,                                                 \
            (VAL).unparse(*store, false));                       \
    }                                                            \
                                                                 \
    TEST_F(FIXTURE, Derivation_ ## NAME ## _from_aterm) {        \
        auto parsed = parseDerivation(                           \
            *store,                                              \
            STR,                                                 \
            DRV_NAME,                                            \
            mockXpSettings);                                     \
        ASSERT_EQ(                                               \
            (VAL).toJSON(*store),                                \
            parsed.toJSON(*store));                              \
        ASSERT_EQ(                                               \
            (VAL),                                               \
            parsed);                                             \
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

TEST_JSON(DerivationTest, simple,
    R"({
      "name": "simple-derivation",
      "inputSrcs": [
        "/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep1"
      ],
      "inputDrvs": {
        "/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep2.drv": {
          "dynamicOutputs": {},
          "outputs": [
            "cat",
            "dog"
          ]
        }
      },
      "system": "wasm-sel4",
      "builder": "foo",
      "args": [
        "bar",
        "baz"
      ],
      "env": {
        "BIG_BAD": "WOLF"
      },
      "outputs": {}
    })",
    makeSimpleDrv(*store))

TEST_ATERM(DerivationTest, simple,
    R"(Derive([],[("/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep2.drv",["cat","dog"])],["/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep1"],"wasm-sel4","foo",["bar","baz"],[("BIG_BAD","WOLF")]))",
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

TEST_JSON(DynDerivationTest, dynDerivationDeps,
    R"({
      "name": "dyn-dep-derivation",
      "inputSrcs": [
        "/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep1"
      ],
      "inputDrvs": {
        "/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep2.drv": {
          "dynamicOutputs": {
            "cat": {
              "dynamicOutputs": {},
              "outputs": ["kitten"]
            },
            "goose": {
              "dynamicOutputs": {},
              "outputs": ["gosling"]
            }
          },
          "outputs": [
            "cat",
            "dog"
          ]
        }
      },
      "system": "wasm-sel4",
      "builder": "foo",
      "args": [
        "bar",
        "baz"
      ],
      "env": {
        "BIG_BAD": "WOLF"
      },
      "outputs": {}
    })",
    makeDynDepDerivation(*store))

TEST_ATERM(DynDerivationTest, dynDerivationDeps,
    R"(DrvWithVersion("xp-dyn-drv",[],[("/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep2.drv",(["cat","dog"],[("cat",["kitten"]),("goose",["gosling"])]))],["/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep1"],"wasm-sel4","foo",["bar","baz"],[("BIG_BAD","WOLF")]))",
    makeDynDepDerivation(*store),
    "dyn-dep-derivation")

#undef TEST_JSON
#undef TEST_ATERM

}
