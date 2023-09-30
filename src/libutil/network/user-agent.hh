#pragma once
// @file

#include <curl/curl.h>
#include <string_view>

namespace nix {

/*
 * Sets the user agent of a `CURL*` handle
 * to the uniform `curl/$curl_version Nix/$nix_version`
 * and concatenates a suffix if non-empty.
 */
void set_user_agent(CURL *handle, std::string_view user_agent_suffix);
}
