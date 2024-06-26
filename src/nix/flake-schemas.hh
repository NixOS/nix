#include "eval-cache.hh"
#include "flake/flake.hh"
#include "command.hh"

namespace nix::flake_schemas {

using namespace eval_cache;

std::tuple<ref<eval_cache::EvalCache>, ref<AttrCursor>>
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

/* Some helper functions for processing flake schema output. */
struct MixFlakeSchemas : virtual Args, virtual StoreCommand
{
    std::optional<std::string> defaultFlakeSchemas;

    MixFlakeSchemas();

    std::optional<FlakeRef> getDefaultFlakeSchemas();
};

}
