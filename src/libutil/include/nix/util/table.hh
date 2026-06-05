#pragma once

#include "nix/util/types.hh"

#include <limits>

namespace nix {

struct TableCell
{
    std::string content;

    enum Alignment { Left, Right } alignment = Left;

    TableCell(std::string content, Alignment alignment = Left)
        : content(std::move(content))
        , alignment(alignment)
    {
    }
};

using TableRow = std::vector<TableCell>;
using Table = std::vector<TableRow>;

void printTable(std::ostream & out, Table & table, unsigned int width = std::numeric_limits<unsigned int>::max());

} // namespace nix
