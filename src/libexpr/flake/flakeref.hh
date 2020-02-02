#pragma once

#include "types.hh"
#include "hash.hh"
#include "fetchers/fetchers.hh"

#include <variant>

namespace nix {

class Store;

typedef std::string FlakeId;

struct FlakeRef
{
    std::shared_ptr<const fetchers::Input> input;

    Path subdir;

    bool operator==(const FlakeRef & other) const;

    FlakeRef(const std::shared_ptr<const fetchers::Input> & input, const Path & subdir)
        : input(input), subdir(subdir)
    {
        assert(input);
    }

    // FIXME: change to operator <<.
    std::string to_string() const;

    fetchers::Input::Attrs toAttrs() const;

    FlakeRef resolve(ref<Store> store) const;

    static FlakeRef fromAttrs(const fetchers::Input::Attrs & attrs);

    std::pair<fetchers::Tree, FlakeRef> fetchTree(ref<Store> store) const;
};

std::ostream & operator << (std::ostream & str, const FlakeRef & flakeRef);

FlakeRef parseFlakeRef(
    const std::string & url, const std::optional<Path> & baseDir = {});

std::optional<FlakeRef> maybeParseFlake(
    const std::string & url, const std::optional<Path> & baseDir = {});

std::pair<FlakeRef, std::string> parseFlakeRefWithFragment(
    const std::string & url, const std::optional<Path> & baseDir = {});

std::optional<std::pair<FlakeRef, std::string>> maybeParseFlakeRefWithFragment(
    const std::string & url, const std::optional<Path> & baseDir = {});

}
