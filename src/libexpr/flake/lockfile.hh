#pragma once

#include "flakeref.hh"

#include <nlohmann/json_fwd.hpp>

namespace nix {
class Store;
class StorePath;
}

namespace nix::flake {

typedef std::vector<FlakeId> InputPath;

struct LockedNode;

/* A node in the lock file. It has outgoing edges to other nodes (its
   inputs). Only the root node has this type; all other nodes have
   type LockedNode. */
struct Node : std::enable_shared_from_this<Node>
{
    typedef std::variant<std::shared_ptr<LockedNode>, InputPath> Edge;

    std::map<FlakeId, Edge> inputs;

    virtual ~Node() { }
};

/* A non-root node in the lock file. */
struct LockedNode : Node
{
    FlakeRef lockedRef, originalRef;
    bool isFlake = true;

    /* The node relative to which relative source paths
       (e.g. 'path:../foo') are interpreted. */
    std::optional<InputPath> parentPath;

    LockedNode(
        const FlakeRef & lockedRef,
        const FlakeRef & originalRef,
        bool isFlake = true,
        std::optional<InputPath> parentPath = {})
        : lockedRef(lockedRef), originalRef(originalRef), isFlake(isFlake), parentPath(parentPath)
    { }

    LockedNode(const nlohmann::json & json);
};

struct LockFile
{
    std::shared_ptr<Node> root = std::make_shared<Node>();

    LockFile() {};
    LockFile(std::string_view contents, std::string_view path);

    nlohmann::json toJSON() const;

    std::string to_string() const;

    static LockFile read(const Path & path);

    void write(const Path & path) const;

    /* Check whether this lock file has any unlocked inputs. If so,
       return one. */
    std::optional<FlakeRef> isUnlocked() const;

    bool operator ==(const LockFile & other) const;

    std::shared_ptr<Node> findInput(const InputPath & path);

    std::map<InputPath, Node::Edge> getAllInputs() const;

    static std::string diff(const LockFile & oldLocks, const LockFile & newLocks);

    /* Check that every 'follows' input target exists. */
    void check();
};

std::ostream & operator <<(std::ostream & stream, const LockFile & lockFile);

InputPath parseInputPath(std::string_view s);

std::string printInputPath(const InputPath & path);

}
