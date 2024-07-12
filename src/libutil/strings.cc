#include <string>

#include "strings-inline.hh"
#include "util.hh"

namespace nix {

template std::string concatStringsSep(std::string_view, const Strings &);
template std::string concatStringsSep(std::string_view, const StringSet &);
template std::string concatStringsSep(std::string_view, const std::vector<std::string> &);

} // namespace nix
