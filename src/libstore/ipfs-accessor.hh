#pragma once

#include <functional>
#include <memory>
#include "ipfs.hh"
#include "config.h"

namespace nix {

class IPFSAccessor {
public:
  IPFSAccessor();

  static void getFile(const std::string & hash,
                           std::function<void(std::shared_ptr<std::string>)> success,
                           std::function<void(std::exception_ptr exc)> failure);
  static std::shared_ptr<std::string> getFile(const std::string & hash);
  static std::string addFile(const std::string & filename, const std::string & content);

};

}
