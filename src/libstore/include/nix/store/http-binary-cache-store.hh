#pragma once
///@file

#include "nix/util/types.hh"
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

struct HttpBinaryCacheStoreConfig : std::enable_shared_from_this<HttpBinaryCacheStoreConfig>,
                                    virtual Store::Config,
                                    BinaryCacheStoreConfig
{
    using BinaryCacheStoreConfig::BinaryCacheStoreConfig;

    HttpBinaryCacheStoreConfig(
        std::string_view scheme, std::string_view cacheUri, const Store::Config::Params & params);

    Path cacheUri;

    static const std::string name()
    {
        return "HTTP Binary Cache Store";
    }

    static StringSet uriSchemes();

    const Setting<std::string> authmethod{this, "basic", "authmethod",
        R"(
          libcurl auth method to use (`none`, `basic`, `digest`, `bearer`, `negotiate`, `ntlm`, `any`, or `anysafe`).
          See https://curl.se/libcurl/c/CURLOPT_HTTPAUTH.html for more info.
        )"};

    const Setting<std::string> bearer_token{this, "", "bearer-token",
        "Bearer token to use for authentication. Requires `authmethod` to be set to `bearer`."};

    static std::string doc();

    ref<Store> openStore() const override;
};

}
