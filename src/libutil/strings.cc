#include <filesystem>
#include <string>

#include "strings-inline.hh"
#include "os-string.hh"

namespace nix {

template std::list<std::string> tokenizeString(std::string_view s, std::string_view separators);
template std::set<std::string> tokenizeString(std::string_view s, std::string_view separators);
template std::vector<std::string> tokenizeString(std::string_view s, std::string_view separators);

template std::list<std::string> splitString(std::string_view s, std::string_view separators);
template std::set<std::string> splitString(std::string_view s, std::string_view separators);
template std::vector<std::string> splitString(std::string_view s, std::string_view separators);

template std::list<OsString>
basicSplitString(std::basic_string_view<OsChar> s, std::basic_string_view<OsChar> separators);

template std::string concatStringsSep(std::string_view, const std::list<std::string> &);
template std::string concatStringsSep(std::string_view, const std::set<std::string> &);
template std::string concatStringsSep(std::string_view, const std::vector<std::string> &);

typedef std::string_view strings_2[2];
template std::string concatStringsSep(std::string_view, const strings_2 &);
typedef std::string_view strings_3[3];
template std::string concatStringsSep(std::string_view, const strings_3 &);
typedef std::string_view strings_4[4];
template std::string concatStringsSep(std::string_view, const strings_4 &);

template std::string dropEmptyInitThenConcatStringsSep(std::string_view, const std::list<std::string> &);
template std::string dropEmptyInitThenConcatStringsSep(std::string_view, const std::set<std::string> &);
template std::string dropEmptyInitThenConcatStringsSep(std::string_view, const std::vector<std::string> &);

} // namespace nix
