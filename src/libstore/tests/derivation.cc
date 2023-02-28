#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include "derivations.hh"

#include "tests/libstore.hh"

namespace nix {

class DerivationTest : public LibStoreTest
{
};

#define TEST_JSON(TYPE, NAME, STR, VAL, ...)                           \
    TEST_F(DerivationTest, TYPE ## _ ## NAME ## _to_json) {            \
        using nlohmann::literals::operator "" _json;                   \
        ASSERT_EQ(                                                     \
            STR ## _json,                                              \
            (TYPE { VAL }).toJSON(*store __VA_OPT__(,) __VA_ARGS__));  \
    }

TEST_JSON(DerivationOutput, inputAddressed,
    R"({
        "path": "/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-drv-name-output-name"
    })",
    (DerivationOutput::InputAddressed {
        .path = store->parseStorePath("/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-drv-name-output-name"),
    }),
    "drv-name", "output-name")

TEST_JSON(DerivationOutput, caFixed,
    R"({
        "hashAlgo": "r:sha256",
        "hash": "894517c9163c896ec31a2adbd33c0681fd5f45b2c0ef08a64c92a03fb97f390f",
        "path": "/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-drv-name-output-name"
    })",
    (DerivationOutput::CAFixed {
        .ca = FixedOutputInfo {
            .hash = {
                .method = FileIngestionMethod::Recursive,
                .hash = Hash::parseAnyPrefixed("sha256-iUUXyRY8iW7DGirb0zwGgf1fRbLA7wimTJKgP7l/OQ8="),
            },
            .references = {},
        },
    }),
    "drv-name", "output-name")

TEST_JSON(DerivationOutput, caFixedText,
    R"({
        "hashAlgo": "text:sha256",
        "hash": "894517c9163c896ec31a2adbd33c0681fd5f45b2c0ef08a64c92a03fb97f390f",
        "path": "/nix/store/6s1zwabh956jvhv4w9xcdb5jiyanyxg1-drv-name-output-name"
    })",
    (DerivationOutput::CAFixed {
        .ca = TextInfo {
            .hash = {
                .hash = Hash::parseAnyPrefixed("sha256-iUUXyRY8iW7DGirb0zwGgf1fRbLA7wimTJKgP7l/OQ8="),
            },
            .references = {},
        },
    }),
    "drv-name", "output-name")

TEST_JSON(DerivationOutput, caFloating,
    R"({
        "hashAlgo": "r:sha256"
    })",
    (DerivationOutput::CAFloating {
        .method = FileIngestionMethod::Recursive,
        .hashType = htSHA256,
    }),
    "drv-name", "output-name")

TEST_JSON(DerivationOutput, deferred,
    R"({ })",
    DerivationOutput::Deferred { },
    "drv-name", "output-name")

TEST_JSON(DerivationOutput, impure,
    R"({
        "hashAlgo": "r:sha256",
        "impure": true
    })",
    (DerivationOutput::Impure {
        .method = FileIngestionMethod::Recursive,
        .hashType = htSHA256,
    }),
    "drv-name", "output-name")

TEST_JSON(Derivation, impure,
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
    }))

#undef TEST_JSON

}
