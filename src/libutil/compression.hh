#pragma once

#include "ref.hh"
#include "types.hh"

#include <string>

namespace nix {

ref<std::string> compress(const std::string & method, ref<std::string> in);

ref<std::string> decompress(const std::string & method, ref<std::string> in);

MakeError(UnknownCompressionMethod, Error);

}
