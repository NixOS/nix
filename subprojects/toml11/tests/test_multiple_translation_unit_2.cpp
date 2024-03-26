#include <toml.hpp>

int read_a(const toml::table& t)
{
    return toml::get<int>(t.at("a"));
}
