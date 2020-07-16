#pragma once

#include "types.hh"
#include "hash.hh"
#include "fetchers.hh"

#include <variant>

namespace nix {

class Store;

typedef std::string FlakeId;

struct FlakeRef
{
    fetchers::Input input;

    Path subdir;

    bool operator==(const FlakeRef & other) const;

    FlakeRef(fetchers::Input && input, const Path & subdir)
        : input(std::move(input)), subdir(subdir)
    { }

    // FIXME: change to operator <<.
    std::string to_string() const;

    fetchers::Attrs toAttrs() const;

    FlakeRef resolve(ref<Store> store) const;

    static FlakeRef fromAttrs(const fetchers::Attrs & attrs);

    std::pair<fetchers::Tree, FlakeRef> fetchTree(ref<Store> store) const;
};

std::ostream & operator << (std::ostream & str, const FlakeRef & flakeRef);

FlakeRef parseFlakeRef(
    const std::string & url, const std::optional<Path> & baseDir = {}, bool allowMissing = false);

std::optional<FlakeRef> maybeParseFlake(
    const std::string & url, const std::optional<Path> & baseDir = {});

std::pair<FlakeRef, std::string> parseFlakeRefWithFragment(
    const std::string & url, const std::optional<Path> & baseDir = {}, bool allowMissing = false);

std::optional<std::pair<FlakeRef, std::string>> maybeParseFlakeRefWithFragment(
    const std::string & url, const std::optional<Path> & baseDir = {});

}
