#pragma once

#include "ref.hh"

#include <string>

namespace nix {

std::string compressXZ(const std::string & in);

ref<std::string> decompressXZ(const std::string & in);

}
