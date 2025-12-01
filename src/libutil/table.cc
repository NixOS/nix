#include "nix/util/table.hh"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

namespace nix {

void printTable(Table & table)
{
    auto nrColumns = table.size() > 0 ? table.front().size() : 0;

    std::vector<size_t> widths;
    widths.resize(nrColumns);

    for (auto & i : table) {
        assert(i.size() == nrColumns);
        size_t column = 0;
        for (auto j = i.begin(); j != i.end(); ++j, ++column)
            if (j->size() > widths[column])
                widths[column] = j->size();
    }

    for (auto & i : table) {
        size_t column = 0;
        for (auto j = i.begin(); j != i.end(); ++j, ++column) {
            std::string s = *j;
            replace(s.begin(), s.end(), '\n', ' ');
            std::cout << s;
            if (column < nrColumns - 1)
                std::cout << std::string(widths[column] - s.size() + 2, ' ');
        }
        std::cout << std::endl;
    }
}

} // namespace nix
