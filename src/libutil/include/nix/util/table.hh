#pragma once

#include "nix/util/types.hh"

#include <list>

namespace nix {

typedef std::list<Strings> Table;

void printTable(Table & table);

} // namespace nix
