#include "user-agent.hh"
#include "libstore/globals.hh"

namespace nix {

void set_user_agent(CURL *handle, std::string_view user_agent_suffix)
{
    curl_easy_setopt(
        handle,
        CURLOPT_USERAGENT,
        (std::string { "curl/" LIBCURL_VERSION "Nix/" PACKAGE_VERSION }
            + (user_agent_suffix != "" ? " " : "")
            + std::string { user_agent_suffix }).c_str());
}

}
