#include <windows.h>
#include <toml.hpp>
#include <iostream>

int main()
{
    using namespace toml::literals::toml_literals;
    const auto data = R"(windows = "defines min and max as a macro")"_toml;

    std::cout << toml::find<std::string>(data, "windows") << std::endl;
    return 0;
}
