#include <toml.hpp>

#include <iostream>
#include <iomanip>

int main(int argc, char **argv)
{
    if(argc != 3)
    {
        std::cerr << "usage: ./check [filename] [valid|invalid]" << std::endl;
        return 1;
    }

    const std::string file_kind(argv[2]);

    try
    {
        const auto data = toml::parse(argv[1]);
        std::cout << std::setprecision(16) << std::setw(80) << data;
        if(file_kind == "valid")
        {
            return 0;
        }
        else
        {
            return 1;
        }
    }
    catch(const toml::syntax_error& err)
    {
        std::cout << "what(): " << err.what() << std::endl;
        if(file_kind == "invalid")
        {
            return 0;
        }
        else
        {
            return 1;
        }
    }
    return 127;
}
