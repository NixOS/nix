#include <gtest/gtest.h>
#include "fetchers.hh"
#include "json-utils.hh"

namespace nix {
    TEST(PublicKey, jsonSerialization) {
        auto json = nlohmann::json(fetchers::PublicKey { .key = "ABCDE" });

        ASSERT_EQ(json, R"({ "key": "ABCDE", "type": "ssh-ed25519" })"_json);
    }
    TEST(PublicKey, jsonDeserialization) {
        auto pubKeyJson = R"({ "key": "ABCDE", "type": "ssh-ed25519" })"_json;
        fetchers::PublicKey pubKey = pubKeyJson;

        ASSERT_EQ(pubKey.key, "ABCDE");
        ASSERT_EQ(pubKey.type, "ssh-ed25519");
    }
}
