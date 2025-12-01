#pragma once

#include "nix/util/types.hh"

namespace nix {

typedef std::vector<std::vector<std::string>> Table;

void printTable(std::ostream & out, Table & table);

} // namespace nix
