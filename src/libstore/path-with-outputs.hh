#pragma once

#include "path.hh"

namespace nix {

struct StorePathWithOutputs
{
    StorePath path;
    std::set<std::string> outputs;

    std::string to_string(const Store & store) const;
};

std::pair<std::string_view, StringSet> parsePathWithOutputs(std::string_view s);

}
