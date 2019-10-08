#include "util.hh"

namespace nix {

std::string rewriteStrings(std::string s, const StringRewrites & rewrites)
{
    for (auto & i : rewrites) {
        size_t j = 0;
        while ((j = s.find(i.first, j)) != string::npos)
            s.replace(j, i.first.size(), i.second);
    }
    return s;
}

}
