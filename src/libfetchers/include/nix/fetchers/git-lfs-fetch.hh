#pragma once
///@file

#include "nix/util/canon-path.hh"
#include "nix/util/serialise.hh"
#include "nix/util/url.hh"

#include <git2/repository.h>

#include <nlohmann/json_fwd.hpp>

namespace nix::lfs {

/**
 * git-lfs pointer
 * @see https://github.com/git-lfs/git-lfs/blob/2ef4108/docs/spec.md
 */
struct Pointer
{
    std::string oid; // git-lfs managed object id. you give this to the lfs server
                     // for downloads
    size_t size;     // in bytes
};

struct Fetch
{
    // Reference to the repository
    const git_repository * repo;

    // Git commit being fetched
    git_oid rev;

    // derived from git remote url
    nix::ParsedURL url;

    Fetch(git_repository * repo, git_oid rev);
    bool shouldFetch(const CanonPath & path) const;
    void fetch(
        const std::string & content,
        const CanonPath & pointerFilePath,
        StringSink & sink,
        std::function<void(uint64_t)> sizeCallback) const;
    std::vector<nlohmann::json> fetchUrls(const std::vector<Pointer> & pointers) const;
};

} // namespace nix::lfs
