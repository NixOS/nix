#pragma once

#include <variant>

#include "path.hh"
#include "derived-path.hh"

namespace nix {

struct StorePathWithOutputs
{
    StorePath path;
    std::set<std::string> outputs;

    std::string to_string(const Store & store) const;

    DerivedPath toDerivedPath() const;

    typedef std::variant<StorePathWithOutputs, StorePath, std::monostate> ParseResult;

    static StorePathWithOutputs::ParseResult tryFromDerivedPath(const DerivedPath &);
};

std::vector<DerivedPath> toDerivedPaths(const std::vector<StorePathWithOutputs>);

std::pair<std::string_view, StringSet> parsePathWithOutputs(std::string_view s);

class Store;

/* Split a string specifying a derivation and a set of outputs
   (/nix/store/hash-foo!out1,out2,...) into the derivation path
   and the outputs. */
StorePathWithOutputs parsePathWithOutputs(const Store & store, std::string_view pathWithOutputs);

StorePathWithOutputs followLinksToStorePathWithOutputs(const Store & store, std::string_view pathWithOutputs);

}
