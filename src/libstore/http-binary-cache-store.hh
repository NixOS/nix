#pragma once
///@file

#include "types.hh"
#include <curl/curl.h>

#include "binary-cache-store.hh"

namespace nix {

enum struct HttpAuthMethod : unsigned long {
    NONE = CURLAUTH_NONE,
    BASIC = CURLAUTH_BASIC,
    DIGEST = CURLAUTH_DIGEST,
    NEGOTIATE = CURLAUTH_NEGOTIATE,
    NTLM = CURLAUTH_NTLM,
    BEARER = CURLAUTH_BEARER,
    ANY = CURLAUTH_ANY,
    ANYSAFE = CURLAUTH_ANYSAFE
};

struct HttpBinaryCacheStoreConfig : virtual BinaryCacheStoreConfig
{
    using BinaryCacheStoreConfig::BinaryCacheStoreConfig;

    HttpBinaryCacheStoreConfig(std::string_view scheme, std::string_view _cacheUri, const Params & params);

    Path cacheUri;

    const std::string name() override
    {
        return "HTTP Binary Cache Store";
    }

    static std::set<std::string> uriSchemes()
    {
        static bool forceHttp = getEnv("_NIX_FORCE_HTTP") == "1";
        auto ret = std::set<std::string>({"http", "https"});
        if (forceHttp)
            ret.insert("file");
        return ret;
    }

    const Setting<std::string> authmethod{this, "basic", "authmethod",
        R"(
          libcurl auth method to use (`none`, `basic`, `digest`, `bearer`, `negotiate`, `ntlm`, `any`, or `anysafe`).
          See https://curl.se/libcurl/c/CURLOPT_HTTPAUTH.html for more info.
        )"};

    const Setting<std::string> bearer_token{this, "", "bearer-token",
        "Bearer token to use for authentication. Requires `authmethod` to be set to `bearer`."};

    std::string doc() override;
};

}
