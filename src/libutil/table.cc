#include "nix/util/table.hh"
#include "nix/util/terminal.hh"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

namespace nix {

void printTable(std::ostream & out, Table & table, unsigned int width)
{
    auto nrColumns = table.size() > 0 ? table.front().size() : 0;

    std::vector<size_t> widths;
    widths.resize(nrColumns);

    for (auto & i : table) {
        assert(i.size() == nrColumns);
        size_t column = 0;
        for (auto j = i.begin(); j != i.end(); ++j, ++column)
            // TODO: take ANSI escapes into account when calculating width.
            widths[column] = std::max(widths[column], j->content.size());
    }

    for (auto & i : table) {
        size_t column = 0;
        std::string line;
        for (auto j = i.begin(); j != i.end(); ++j, ++column) {
            std::string s = j->content;
            replace(s.begin(), s.end(), '\n', ' ');

            auto padding = std::string(widths[column] - s.size(), ' ');
            if (j->alignment == TableCell::Right) {
                line += padding;
                line += s;
            } else {
                line += s;
                if (column + 1 < nrColumns)
                    line += padding;
            }

            if (column + 1 < nrColumns)
                line += "  ";
        }
        out << filterANSIEscapes(line, false, width);
        out << std::endl;
    }
}

} // namespace nix
