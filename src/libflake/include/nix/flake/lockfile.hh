#pragma once
///@file

#include "nix/flake/flakeref.hh"

#include <nlohmann/json_fwd.hpp>

namespace nix {
class Store;
class StorePath;
} // namespace nix

namespace nix::flake {

typedef std::vector<FlakeId> InputAttrPath;

struct LockedNode;

/**
 * A node in the lock file. It has outgoing edges to other nodes (its
 * inputs). Only the root node has this type; all other nodes have
 * type LockedNode.
 */
struct Node : std::enable_shared_from_this<Node>
{
    typedef std::variant<ref<LockedNode>, InputAttrPath> Edge;

    std::map<FlakeId, Edge> inputs;

    virtual ~Node() {}
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
    std::optional<InputAttrPath> parentInputAttrPath;

    LockedNode(
        const FlakeRef & lockedRef,
        const FlakeRef & originalRef,
        bool isFlake = true,
        std::optional<InputAttrPath> parentInputAttrPath = {})
        : lockedRef(std::move(lockedRef))
        , originalRef(std::move(originalRef))
        , isFlake(isFlake)
        , parentInputAttrPath(std::move(parentInputAttrPath))
    {
    }

    LockedNode(const fetchers::Settings & fetchSettings, const nlohmann::json & json);

    StorePath computeStorePath(Store & store) const;
};

struct LockFile
{
    ref<Node> root = make_ref<Node>();

    LockFile() {};
    LockFile(const fetchers::Settings & fetchSettings, std::string_view contents, std::string_view path);

    typedef std::map<ref<const Node>, std::string> KeyMap;

    std::pair<nlohmann::json, KeyMap> toJSON() const;

    std::pair<std::string, KeyMap> to_string() const;

    /**
     * Check whether this lock file has any unlocked or non-final
     * inputs. If so, return one.
     */
    std::optional<FlakeRef> isUnlocked(const fetchers::Settings & fetchSettings) const;

    bool operator==(const LockFile & other) const;

    std::shared_ptr<Node> findInput(const InputAttrPath & path);

    std::map<InputAttrPath, Node::Edge> getAllInputs() const;

    static std::string diff(const LockFile & oldLocks, const LockFile & newLocks);

    /**
     * Check that every 'follows' input target exists.
     */
    void check();
};

std::ostream & operator<<(std::ostream & stream, const LockFile & lockFile);

InputAttrPath parseInputAttrPath(std::string_view s);

std::string printInputAttrPath(const InputAttrPath & path);

} // namespace nix::flake
