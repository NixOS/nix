#pragma once

#include "nix/expr/eval-cache.hh"
#include "nix/flake/flake.hh"
#include "nix/cmd/command.hh"

namespace nix::flake_schemas {

using namespace eval_cache;

ref<eval_cache::EvalCache> call(
    EvalState & state,
    std::shared_ptr<flake::LockedFlake> lockedFlake,
    std::optional<FlakeRef> defaultSchemasFlake,
    bool allowEvalCache = true);

void forEachOutput(
    ref<AttrCursor> inventory,
    std::function<void(Symbol outputName, std::shared_ptr<AttrCursor> output, const std::string & doc, bool isLast)> f);

/**
 * A convenience wrapper around `AttrCursor` for nodes in the `inventory` tree returned by call-flake-schemas.nix.
 */
struct Node
{
    const ref<AttrCursor> node;

    Node(const ref<AttrCursor> & node)
        : node(node)
    {
    }

    /**
     * Return the `forSystems` attribute. This can be null, which
     * means "all systems".
     */
    std::optional<std::vector<std::string>> forSystems() const;

    /**
     * Return the actual output corresponding to this info node.
     */
    ref<AttrCursor> getOutput(const ref<AttrCursor> & outputs) const;
};

struct Leaf : Node
{
    using Node::Node;

    std::optional<std::string> what() const;

    std::optional<std::string> shortDescription() const;

    std::optional<AttrPath> derivationAttrPath() const;

    /**
     * Return the attribute corresponding to `derivationAttrPath`, if set.
     */
    std::shared_ptr<AttrCursor> derivation(const ref<AttrCursor> & outputs) const;

    bool isFlakeCheck() const;
};

typedef std::function<void(Symbol attrName, ref<AttrCursor> attr, bool isLast)> ForEachChild;

void visit(
    std::optional<std::string> system,
    bool includeLegacy,
    ref<AttrCursor> node,
    std::function<void(const Leaf & leaf)> visitLeaf,
    std::function<void(std::function<void(ForEachChild)>)> visitNonLeaf,
    std::function<void(ref<AttrCursor> node, const std::vector<std::string> & systems)> visitFiltered,
    std::function<void(ref<AttrCursor> node)> visitLegacy);

struct OutputInfo
{
    ref<AttrCursor> schemaInfo;
    ref<AttrCursor> nodeInfo;
    AttrPath leafAttrPath;
};

std::optional<OutputInfo> getOutputInfo(ref<AttrCursor> inventory, AttrPath attrPath);

struct SchemaInfo
{
    std::string doc;
    StringSet roles;
    bool appendSystem = false;
    std::optional<AttrPath> defaultAttrPath;
};

using Schemas = std::map<std::string, SchemaInfo>;

Schemas getSchemas(ref<AttrCursor> root);

} // namespace nix::flake_schemas
