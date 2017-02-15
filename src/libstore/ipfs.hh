#pragma once

#include <string>
#include <cstdint>

namespace ipfs {

inline std::string buildAPIURL(const std::string & host,
                               uint16_t port = 5001,
                               const std::string & version = "v0")
{
  return "http://" + host + ":" + std::to_string(port) + "/api/" + version;
}

inline std::string buildCommandURL(const std::string & cmd, const std::string & arg) {
  if (cmd == "cat")
    return "/cat/" + arg;
  else if (cmd == "block/get")
    return "/block/get?" + arg;
  else if (cmd == "cat_gw")
    return "/ipfs/" + arg;
  else
    throw "No such command";
}

}
