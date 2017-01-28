#pragma once

#include <string>
#include <cstdint>

#include "types.hh"
#include "download.hh"

namespace nix {
namespace ipfs {

MakeError (CommandError, Error);

inline std::string buildAPIURL(const std::string & host,
                               uint16_t port = 5001,
                               const std::string & version = "v0")
{
    return "http://" + host + ":" + std::to_string(port) + "/api/" + version;
}

inline std::string buildQuery(const std::vector<std::pair<std::string, std::string>> & params = {}) {
    std::string query = "?stream-channels=true&json=true&encoding=json";
    for (auto& param : params) {
      std::string key = getDownloader()->urlEncode(param.first);
      std::string value = getDownloader()->urlEncode(param.second);
      query += "&" + key + "=" + value;
    }
    return query;
}

}
}
