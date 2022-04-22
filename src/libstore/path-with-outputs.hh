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

    static std::variant<StorePathWithOutputs, StorePath> tryFromDerivedPath(const DerivedPath &);
};

std::vector<DerivedPath> toDerivedPaths(const std::vector<StorePathWithOutputs>);

std::pair<std::string_view, StringSet> parsePathWithOutputs(std::string_view s);

class Store;

/* Split a string specifying a derivation and a set of outputs
   (/nix/store/hash-foo!out1,out2,...) into the derivation path
   and the outputs. */
StorePathWithOutputs parsePathWithOutputs(const Store & store, std::string_view pathWithOutputs);

StorePathWithOutputs followLinksToStorePathWithOutputs(const Store & store, std::string_view pathWithOutputs);

typedef std::set<std::string> OutputNames;

struct AllOutputs { };

struct DefaultOutputs { };

typedef std::variant<DefaultOutputs, AllOutputs, OutputNames> OutputsSpec;

/* Parse a string of the form 'prefix^output1,...outputN' or
   'prefix^*', returning the prefix and the outputs spec. */
std::pair<std::string, OutputsSpec> parseOutputsSpec(const std::string & s);

}
