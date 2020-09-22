#pragma once

#include "flakeref.hh"
#include "content-address.hh"

#include <nlohmann/json_fwd.hpp>

namespace nix {
class Store;
class StorePath;
}

namespace nix::flake {

using namespace fetchers;

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

    LockedNode(
        const FlakeRef & lockedRef,
        const FlakeRef & originalRef,
        bool isFlake = true)
        : lockedRef(lockedRef), originalRef(originalRef), isFlake(isFlake)
    { }

    LockedNode(const nlohmann::json & json);

    StorePathDescriptor computeStorePath(Store & store) const;
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
