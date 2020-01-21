#pragma once

#include "types.hh"
#include "hash.hh"

#include <variant>

namespace nix {

class Store;

namespace fetchers { struct Input; }

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

    /* Check whether this is a "direct" flake reference, that is, not
       a flake ID, which requires a lookup in the flake registry. */
    bool isDirect() const;

    /* Check whether this is an "immutable" flake reference, that is,
       one that contains a commit hash or content hash. */
    bool isImmutable() const;

    FlakeRef resolve(ref<Store> store) const;
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
