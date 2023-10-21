#include <vector>
#include <optional>

#include <gtest/gtest.h>

#include "json-utils.hh"

namespace nix {

  /* Test `to_json` and `from_json` with `std::optional` types.
   * We are specifically interested in whether we can _nest_ optionals in STL
   * containers so we that we can leverage existing adl_serializer templates. */

    TEST(to_json, optionalInt) {
        std::optional<int> val = std::make_optional(420);
        ASSERT_EQ(nlohmann::json(val), nlohmann::json(420));
        val = std::nullopt;
        ASSERT_EQ(nlohmann::json(val), nlohmann::json(nullptr));
    }

    TEST(to_json, vectorOfOptionalInts) {
        std::vector<std::optional<int>> vals = {
          std::make_optional(420)
        , std::nullopt
        };
        ASSERT_EQ(nlohmann::json(vals), nlohmann::json::parse("[420,null]"));
    }

    TEST(to_json, optionalVectorOfInts) {
        std::optional<std::vector<int>> val = std::make_optional(
            std::vector<int> { -420, 420 }
            );
        ASSERT_EQ(nlohmann::json(val), nlohmann::json::parse("[-420,420]"));
        val = std::nullopt;
        ASSERT_EQ(nlohmann::json(val), nlohmann::json(nullptr));
    }

} /* namespace nix */
