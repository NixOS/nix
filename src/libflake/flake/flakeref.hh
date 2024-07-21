#pragma once
///@file

#include <regex>

#include "types.hh"
#include "fetchers.hh"
#include "outputs-spec.hh"

namespace nix {

class Store;

typedef std::string FlakeId;

/**
 * A flake reference specifies how to fetch a flake or raw source
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
 * The actual fetch may not have been performed yet (i.e. a FlakeRef may
 * be lazy), but the fetcher can be invoked at any time via the
 * FlakeRef to ensure the store is populated with this input.
 */
struct FlakeRef
{
    /**
     * Fetcher-specific representation of the input, sufficient to
     * perform the fetch operation.
     */
    fetchers::Input input;

    /**
     * sub-path within the fetched input that represents this input
     */
    Path subdir;

    bool operator ==(const FlakeRef & other) const = default;

    FlakeRef(fetchers::Input && input, const Path & subdir)
        : input(std::move(input)), subdir(subdir)
    { }

    // FIXME: change to operator <<.
    std::string to_string() const;

    fetchers::Attrs toAttrs() const;

    FlakeRef resolve(ref<Store> store) const;

    static FlakeRef fromAttrs(
        const fetchers::Settings & fetchSettings,
        const fetchers::Attrs & attrs);

    std::pair<StorePath, FlakeRef> fetchTree(ref<Store> store) const;
};

std::ostream & operator << (std::ostream & str, const FlakeRef & flakeRef);

/**
 * @param baseDir Optional [base directory](https://nixos.org/manual/nix/unstable/glossary#gloss-base-directory)
 */
FlakeRef parseFlakeRef(
    const fetchers::Settings & fetchSettings,
    const std::string & url,
    const std::optional<Path> & baseDir = {},
    bool allowMissing = false,
    bool isFlake = true);

/**
 * @param baseDir Optional [base directory](https://nixos.org/manual/nix/unstable/glossary#gloss-base-directory)
 */
std::optional<FlakeRef> maybeParseFlake(
    const fetchers::Settings & fetchSettings,
    const std::string & url,
    const std::optional<Path> & baseDir = {});

/**
 * @param baseDir Optional [base directory](https://nixos.org/manual/nix/unstable/glossary#gloss-base-directory)
 */
std::pair<FlakeRef, std::string> parseFlakeRefWithFragment(
    const fetchers::Settings & fetchSettings,
    const std::string & url,
    const std::optional<Path> & baseDir = {},
    bool allowMissing = false,
    bool isFlake = true);

/**
 * @param baseDir Optional [base directory](https://nixos.org/manual/nix/unstable/glossary#gloss-base-directory)
 */
std::optional<std::pair<FlakeRef, std::string>> maybeParseFlakeRefWithFragment(
    const fetchers::Settings & fetchSettings,
    const std::string & url,
    const std::optional<Path> & baseDir = {});

/**
 * @param baseDir Optional [base directory](https://nixos.org/manual/nix/unstable/glossary#gloss-base-directory)
 */
std::tuple<FlakeRef, std::string, ExtendedOutputsSpec> parseFlakeRefWithFragmentAndExtendedOutputsSpec(
    const fetchers::Settings & fetchSettings,
    const std::string & url,
    const std::optional<Path> & baseDir = {},
    bool allowMissing = false,
    bool isFlake = true);

const static std::string flakeIdRegexS = "[a-zA-Z][a-zA-Z0-9_-]*";
extern std::regex flakeIdRegex;

}
