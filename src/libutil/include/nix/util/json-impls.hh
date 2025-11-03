#pragma once
///@file

#include <nlohmann/json_fwd.hpp>

#include "nix/util/experimental-features.hh"

// Following https://github.com/nlohmann/json#how-can-i-use-get-for-non-default-constructiblenon-copyable-types
#define JSON_IMPL(TYPE)                                   \
    namespace nlohmann {                                  \
    using namespace nix;                                  \
    template<>                                            \
    struct adl_serializer<TYPE>                           \
    {                                                     \
        static TYPE from_json(const json & json);         \
        static void to_json(json & json, const TYPE & t); \
    };                                                    \
    }

#define JSON_IMPL_WITH_XP_FEATURES(TYPE)                                                                            \
    namespace nlohmann {                                                                                            \
    using namespace nix;                                                                                            \
    template<>                                                                                                      \
    struct adl_serializer<TYPE>                                                                                     \
    {                                                                                                               \
        static TYPE                                                                                                 \
        from_json(const json & json, const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings); \
        static void to_json(json & json, const TYPE & t);                                                           \
    };                                                                                                              \
    }
