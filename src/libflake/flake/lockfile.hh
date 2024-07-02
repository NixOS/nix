#pragma once
///@file

#include "flakeref.hh"

#include <nlohmann/json_fwd.hpp>

namespace nix {
class Store;
class StorePath;
}

namespace nix::flake {

typedef std::vector<FlakeId> InputPath;

struct LockedNode;

/**
 * A node in the lock file. It has outgoing edges to other nodes (its
 * inputs). Only the root node has this type; all other nodes have
 * type LockedNode.
 */
struct Node : std::enable_shared_from_this<Node>
{
    typedef std::variant<ref<LockedNode>, InputPath> Edge;

    std::map<FlakeId, Edge> inputs;

    virtual ~Node() { }
};

/**
 * A non-root node in the lock file.
 */
struct LockedNode : Node
{
    FlakeRef lockedRef, originalRef;
    bool isFlake = true;

    /* The node relative to which relative source paths
       (e.g. 'path:../foo') are interpreted. */
    std::optional<InputPath> parentPath;

    std::vector<std::string> patchFiles;

    LockedNode(
        const FlakeRef & lockedRef,
        const FlakeRef & originalRef,
        bool isFlake = true,
        std::optional<InputPath> parentPath = {},
        std::vector<std::string> patchFiles = {})
        : lockedRef(lockedRef), originalRef(originalRef), isFlake(isFlake), parentPath(parentPath), patchFiles(std::move(patchFiles))
    { }

    LockedNode(const nlohmann::json & json);
};

struct LockFile
{
    ref<Node> root = make_ref<Node>();

    LockFile() {};
    LockFile(std::string_view contents, std::string_view path);

    typedef std::map<ref<const Node>, std::string> KeyMap;

    std::pair<nlohmann::json, KeyMap> toJSON() const;

    std::pair<std::string, KeyMap> to_string() const;

    /**
     * Check whether this lock file has any unlocked inputs. If so,
     * return one.
     */
    std::optional<FlakeRef> isUnlocked() const;

    bool operator ==(const LockFile & other) const;
    // Needed for old gcc versions that don't synthesize it (like gcc 8.2.2
    // that is still the default on aarch64-linux)
    bool operator !=(const LockFile & other) const;

    std::shared_ptr<Node> findInput(const InputPath & path);

    std::map<InputPath, Node::Edge> getAllInputs() const;

    static std::string diff(const LockFile & oldLocks, const LockFile & newLocks);

    /**
     * Check that every 'follows' input target exists.
     */
    void check();
};

std::ostream & operator <<(std::ostream & stream, const LockFile & lockFile);

InputPath parseInputPath(std::string_view s);

std::string printInputPath(const InputPath & path);

}
