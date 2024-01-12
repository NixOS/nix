#pragma once
///@file

#include "path.hh"
#include "derived-path.hh"

namespace nix {

struct StoreDirConfig;

/**
 * This is a deprecated old type just for use by the old CLI, and older
 * versions of the RPC protocols. In new code don't use it; you want
 * `DerivedPath` instead.
 *
 * `DerivedPath` is better because it handles more cases, and does so more
 * explicitly without devious punning tricks.
 */
struct StorePathWithOutputs
{
    StorePath path;
    std::set<std::string> outputs;

    std::string to_string(const StoreDirConfig & store) const;

    DerivedPath toDerivedPath() const;

    typedef std::variant<StorePathWithOutputs, StorePath, std::monostate> ParseResult;

    static StorePathWithOutputs::ParseResult tryFromDerivedPath(const DerivedPath &);
};

std::vector<DerivedPath> toDerivedPaths(const std::vector<StorePathWithOutputs>);

std::pair<std::string_view, StringSet> parsePathWithOutputs(std::string_view s);

/**
 * Split a string specifying a derivation and a set of outputs
 * (/nix/store/hash-foo!out1,out2,...) into the derivation path
 * and the outputs.
 */
StorePathWithOutputs parsePathWithOutputs(const StoreDirConfig & store, std::string_view pathWithOutputs);

class Store;

StorePathWithOutputs followLinksToStorePathWithOutputs(const Store & store, std::string_view pathWithOutputs);

}
