#include <toml.hpp>

int read_a(const toml::table&);

int main()
{
    const std::string content("a = 0");
    std::istringstream iss(content);
    const auto data = toml::parse(iss, "test_multiple_translation_unit.toml");
    return read_a(toml::get<toml::table>(data));
}
