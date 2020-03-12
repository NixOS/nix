#pragma once

#include "flakeref.hh"

#include <nlohmann/json_fwd.hpp>

namespace nix {
class Store;
struct StorePath;
}

namespace nix::flake {

using namespace fetchers;

typedef std::vector<FlakeId> InputPath;

/* A node in the lock file. It has outgoing edges to other nodes (its
   inputs). Only the root node has this type; all other nodes have
   type LockedNode. */
struct Node : std::enable_shared_from_this<Node>
{
    std::map<FlakeId, std::shared_ptr<Node>> inputs;

    virtual ~Node() { }

    std::shared_ptr<Node> findInput(const InputPath & path);
};

/* A non-root node in the lock file. */
struct LockedNode : Node
{
    FlakeRef lockedRef, originalRef;
    TreeInfo info;
    bool isFlake = true;

    LockedNode(
        const FlakeRef & lockedRef,
        const FlakeRef & originalRef,
        const TreeInfo & info,
        bool isFlake = true)
        : lockedRef(lockedRef), originalRef(originalRef), info(info), isFlake(isFlake)
    { }

    LockedNode(const nlohmann::json & json);

    StorePath computeStorePath(Store & store) const;
};

struct LockFile
{
    std::shared_ptr<Node> root = std::make_shared<Node>();

    LockFile() {};
    LockFile(const nlohmann::json & json, const Path & path);

    nlohmann::json toJson() const;

    std::string to_string() const;

    static LockFile read(const Path & path);

    void write(const Path & path) const;

    bool isImmutable() const;

    bool operator ==(const LockFile & other) const;
};

std::ostream & operator <<(std::ostream & stream, const LockFile & lockFile);

InputPath parseInputPath(std::string_view s);

std::string diffLockFiles(const LockFile & oldLocks, const LockFile & newLocks);

}

