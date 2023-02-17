#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include "derivations.hh"

#include "tests/libstore.hh"

namespace nix {

class DerivationTest : public LibStoreTest
{
};

#define TEST_JSON(NAME, STR, VAL, DRV_NAME, OUTPUT_NAME) \
    TEST_F(DerivationTest, DerivationOutput_ ## NAME ## _to_json) {    \
        using nlohmann::literals::operator "" _json;           \
        ASSERT_EQ(                                             \
            STR ## _json,                                      \
            (DerivationOutput { VAL }).toJSON(                 \
                *store,                                        \
                DRV_NAME,                                      \
                OUTPUT_NAME));                                 \
    }                                                          \
                                                               \
    TEST_F(DerivationTest, DerivationOutput_ ## NAME ## _from_json) {  \
        using nlohmann::literals::operator "" _json;           \
        ASSERT_EQ(                                             \
            DerivationOutput { VAL },                          \
            DerivationOutput::fromJSON(                        \
                *store,                                        \
                DRV_NAME,                                      \
                OUTPUT_NAME,                                   \
                STR ## _json));                                \
    }

TEST_JSON(inputAddressed,
    R"({
        "path": "/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-drv-name-output-name"
    })",
    (DerivationOutput::InputAddressed {
        .path = store->parseStorePath("/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-drv-name-output-name"),
    }),
    "drv-name", "output-name")

TEST_JSON(caFixed,
    R"({
        "hashAlgo": "r:sha256",
        "hash": "894517c9163c896ec31a2adbd33c0681fd5f45b2c0ef08a64c92a03fb97f390f",
        "path": "/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-drv-name-output-name"
    })",
    (DerivationOutput::CAFixed {
        .hash = {
            .method = FileIngestionMethod::Recursive,
            .hash = Hash::parseAnyPrefixed("sha256-iUUXyRY8iW7DGirb0zwGgf1fRbLA7wimTJKgP7l/OQ8="),
        },
    }),
    "drv-name", "output-name")

TEST_JSON(caFloating,
    R"({
        "hashAlgo": "r:sha256"
    })",
    (DerivationOutput::CAFloating {
        .method = FileIngestionMethod::Recursive,
        .hashType = htSHA256,
    }),
    "drv-name", "output-name")

TEST_JSON(deferred,
    R"({ })",
    DerivationOutput::Deferred { },
    "drv-name", "output-name")

TEST_JSON(impure,
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

#define TEST_JSON(NAME, STR, VAL, DRV_NAME)                     \
    TEST_F(DerivationTest, Derivation_ ## NAME ## _to_json) {   \
        using nlohmann::literals::operator "" _json;            \
        ASSERT_EQ(                                              \
            STR ## _json,                                       \
            (Derivation { VAL }).toJSON(*store));               \
    }                                                           \
                                                                \
    TEST_F(DerivationTest, Derivation_ ## NAME ## _from_json) { \
        using nlohmann::literals::operator "" _json;            \
        ASSERT_EQ(                                              \
            Derivation { VAL },                                 \
            Derivation::fromJSON(                               \
                *store,                                         \
                DRV_NAME,                                       \
                STR ## _json));                                 \
    }

TEST_JSON(simple,
    R"({
      "inputSrcs": [
        "/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep1"
      ],
      "inputDrvs": {
        "/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep2.drv": [
          "cat",
          "dog"
        ]
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
    ({
        Derivation drv;
        drv.inputSrcs = {
            store->parseStorePath("/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep1"),
        };
        drv.inputDrvs = {
            {
                store->parseStorePath("/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep2.drv"),
                {
                    "cat",
                    "dog",
                },
            }
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
        drv;
    }),
    "drv-name")

#undef TEST_JSON

}
