#pragma once

#include "eval-cache.hh"
#include "flake/flake.hh"
#include "command.hh"

namespace nix::flake_schemas {

using namespace eval_cache;

ref<eval_cache::EvalCache>
call(EvalState & state, std::shared_ptr<flake::LockedFlake> lockedFlake, std::optional<FlakeRef> defaultSchemasFlake);

void forEachOutput(
    ref<AttrCursor> inventory,
    std::function<void(Symbol outputName, std::shared_ptr<AttrCursor> output, const std::string & doc, bool isLast)> f);

typedef std::function<void(Symbol attrName, ref<AttrCursor> attr, bool isLast)> ForEachChild;

void visit(
    std::optional<std::string> system,
    ref<AttrCursor> node,
    std::function<void(ref<AttrCursor> leaf)> visitLeaf,
    std::function<void(std::function<void(ForEachChild)>)> visitNonLeaf,
    std::function<void(ref<AttrCursor> node, const std::vector<std::string> & systems)> visitFiltered);

std::optional<std::string> what(ref<AttrCursor> leaf);

std::optional<std::string> shortDescription(ref<AttrCursor> leaf);

std::shared_ptr<AttrCursor> derivation(ref<AttrCursor> leaf);

struct OutputInfo
{
    ref<AttrCursor> schemaInfo;
    ref<AttrCursor> nodeInfo;
    ref<AttrCursor> rawValue;
    eval_cache::AttrPath leafAttrPath;
};

OrSuggestions<OutputInfo> getOutput(ref<AttrCursor> inventory, eval_cache::AttrPath attrPath);

struct SchemaInfo
{
    std::string doc;
    StringSet roles;
    bool appendSystem = false;
    std::optional<eval_cache::AttrPath> defaultAttrPath;
};

using Schemas = std::map<std::string, SchemaInfo>;

Schemas getSchema(ref<AttrCursor> root);

}
