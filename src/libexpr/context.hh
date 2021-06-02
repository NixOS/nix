#include "util.hh"

namespace nix {

/* Decode a context string ‘!<name>!<path>’ into a pair <path,
   name>. */
std::pair<string, string> decodeContext(std::string_view s);

std::string encodeContext(std::string_view name, std::string_view path);

}
