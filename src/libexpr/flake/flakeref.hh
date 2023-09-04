#pragma once

#include "types.hh"
#include "hash.hh"
#include "fetchers.hh"

#include <variant>

namespace nix {

class Store;

typedef std::string FlakeId;

/* A flake reference specifies how to fetch a flake or raw source
 * (e.g. from a Git repository).  It is created from a URL-like syntax
 * (e.g. 'github:NixOS/patchelf'), an attrset representation (e.g. '{
 * type="github"; owner = "NixOS"; repo = "patchelf"; }'), or a local
 * path.
 *
 * Each flake will have a number of FlakeRef objects: one for each
 * input to the flake.
 *
 * The normal method of constructing a FlakeRef is by starting with an
 * input description (usually the attrs or a url from the flake file),
 * locating a fetcher for that input, and then capturing the Input
 * object that fetcher generates (usually via
 * FlakeRef::fromAttrs(attrs) or parseFlakeRef(url) calls).
 *
 * The actual fetch not have been performed yet (i.e. a FlakeRef may
 * be lazy), but the fetcher can be invoked at any time via the
 * FlakeRef to ensure the store is populated with this input.
 */

struct FlakeRef
{
    /* fetcher-specific representation of the input, sufficient to
       perform the fetch operation. */
    fetchers::Input input;

    /* sub-path within the fetched input that represents this input */
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
    const std::string & url,
    const std::optional<Path> & baseDir = {},
    bool allowMissing = false,
    bool isFlake = true);

std::optional<FlakeRef> maybeParseFlake(
    const std::string & url, const std::optional<Path> & baseDir = {});

std::pair<FlakeRef, std::string> parseFlakeRefWithFragment(
    const std::string & url,
    const std::optional<Path> & baseDir = {},
    bool allowMissing = false,
    bool isFlake = true);

std::optional<std::pair<FlakeRef, std::string>> maybeParseFlakeRefWithFragment(
    const std::string & url, const std::optional<Path> & baseDir = {});

}
