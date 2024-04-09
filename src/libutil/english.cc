#include "english.hh"

namespace nix {

std::ostream & pluralize(
    std::ostream & output,
    unsigned int count,
    const std::string_view single,
    const std::string_view plural)
{
    if (count == 1)
        output << "1 " << single;
    else
        output << count << " " << plural;
    return output;
}

}
