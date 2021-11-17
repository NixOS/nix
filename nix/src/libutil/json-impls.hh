#pragma once

#include "nlohmann/json_fwd.hpp"

// Following https://github.com/nlohmann/json#how-can-i-use-get-for-non-default-constructiblenon-copyable-types
#define JSON_IMPL(TYPE)                                                \
    namespace nlohmann {                                               \
        using namespace nix;                                           \
        template <>                                                    \
        struct adl_serializer<TYPE> {                                  \
            static TYPE from_json(const json & json);                  \
            static void to_json(json & json, TYPE t);                  \
        };                                                             \
    }
