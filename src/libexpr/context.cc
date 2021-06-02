#include "context.hh"

namespace nix {

/* Decode a context string ‘!<name>!<path>’ into a pair <path,
   name>. */
std::pair<string, string> decodeContext(std::string_view s)
{
    if (s.at(0) == '!') {
        size_t index = s.find("!", 1);
        return {std::string(s.substr(index + 1)), std::string(s.substr(1, index - 1))};
    } else
        return {s.at(0) == '/' ? std::string(s) : std::string(s.substr(1)), ""};
}

std::string encodeContext(std::string_view name, std::string_view path)
{
    return "!" + std::string(name) + "!" + std::string(path);
}

}
