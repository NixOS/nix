#include "nix/expr/primops.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/fetchers/git-utils.hh"
#include "nix/store/store-api.hh"
#include "nix/fetchers/fetch-to-store.hh"

#include <nlohmann/json.hpp>
#include <set>

namespace nix {

// Helper to get cached manifest JSON (avoids repeated parsing)
static const nlohmann::json & getManifest(EvalState & state)
{
    return state.getManifestJson();
}

// Helper to validate that a zone path exists in the manifest
static void validateZonePath(EvalState & state, const PosIdx pos, std::string_view zonePath)
{
    auto & manifest = getManifest(state);
    if (!manifest.contains(std::string(zonePath)))
        state.error<EvalError>("'%s' is not a zone root (must be an exact path from the manifest)", zonePath)
            .atPos(pos).debugThrow();
}

// ============================================================================
// builtins.worldManifest
// Returns path -> zone metadata mapping from //.meta/manifest.json
// ============================================================================
static void prim_worldManifest(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto json = getManifest(state);

    auto attrs = state.buildBindings(json.size());
    for (auto & [path, value] : json.items()) {
        if (!value.contains("id") || !value.at("id").is_string())
            throw Error("zone '%s' in manifest has missing or non-string 'id' field", path);
        auto idStr = value.at("id").get<std::string>();

        auto zoneAttrs = state.buildBindings(1);
        zoneAttrs.alloc("id").mkString(idStr, state.mem);
        attrs.alloc(state.symbols.create(path)).mkAttrs(zoneAttrs);
    }
    v.mkAttrs(attrs);
}

static RegisterPrimOp primop_worldManifest({
    .name = "__unsafeTectonixInternalManifest",
    .args = {},
    .doc = R"(
      Get the world manifest as a Nix attrset mapping zone paths to zone metadata.

      Example: `builtins.unsafeTectonixInternalManifest."//areas/tools/dev".id` returns `"W-123456"`.

      Uses `--tectonix-git-dir` (defaults to `~/world/git`) and requires
      `--tectonix-git-sha` to be set.
    )",
    .fun = prim_worldManifest,
});

// ============================================================================
// builtins.worldManifestInverted
// Returns zoneId -> path mapping (inverse of worldManifest)
// ============================================================================
static void prim_worldManifestInverted(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto json = getManifest(state);

    // Track seen IDs to detect duplicates
    std::set<std::string> seenIds;

    auto attrs = state.buildBindings(json.size());
    for (auto & [path, value] : json.items()) {
        if (!value.contains("id") || !value.at("id").is_string())
            throw Error("zone '%s' in manifest has missing or non-string 'id' field", path);
        auto idStr = value.at("id").get<std::string>();

        if (!seenIds.insert(idStr).second)
            throw Error("duplicate zone ID '%s' in manifest (zone '%s')", idStr, path);

        attrs.alloc(state.symbols.create(idStr)).mkString(path, state.mem);
    }
    v.mkAttrs(attrs);
}

static RegisterPrimOp primop_worldManifestInverted({
    .name = "__unsafeTectonixInternalManifestInverted",
    .args = {},
    .doc = R"(
      Get the inverted world manifest as a Nix attrset mapping zone IDs to zone paths.

      Example: `builtins.unsafeTectonixInternalManifestInverted."W-123456"` returns `"//areas/tools/dev"`.

      Uses `--tectonix-git-dir` (defaults to `~/world/git`) and requires
      `--tectonix-git-sha` to be set.
    )",
    .fun = prim_worldManifestInverted,
});

// ============================================================================
// builtins.unsafeTectonixInternalTreeSha worldPath
// Returns the git tree SHA for a world path
// ============================================================================
static void prim_unsafeTectonixInternalTreeSha(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto worldPath = state.forceStringNoCtx(*args[0], pos,
        "while evaluating the 'worldPath' argument to builtins.unsafeTectonixInternalTreeSha");

    auto sha = state.getWorldTreeSha(worldPath);
    v.mkString(sha.gitRev(), state.mem);
}

static RegisterPrimOp primop_unsafeTectonixInternalTreeSha({
    .name = "__unsafeTectonixInternalTreeSha",
    .args = {"worldPath"},
    .doc = R"(
      Get the git tree SHA for a path in the world repository.

      Example: `builtins.unsafeTectonixInternalTreeSha "//areas/tools/tec"` returns the tree SHA
      for that zone.

      Uses `--tectonix-git-dir` (defaults to `~/world/git`) and requires
      `--tectonix-git-sha` to be set.
    )",
    .fun = prim_unsafeTectonixInternalTreeSha,
});

// ============================================================================
// builtins.unsafeTectonixInternalTree treeSha
// Returns a store path containing the tree contents
// ============================================================================
static void prim_unsafeTectonixInternalTree(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto treeSha = state.forceStringNoCtx(*args[0], pos,
        "while evaluating the 'treeSha' argument to builtins.unsafeTectonixInternalTree");

    auto repo = state.getWorldRepo();
    auto hash = Hash::parseNonSRIUnprefixed(treeSha, HashAlgorithm::SHA1);

    if (!repo->hasObject(hash))
        state.error<EvalError>("tree SHA '%s' not found in world repository", treeSha)
            .atPos(pos).debugThrow();

    // exportIgnore=false: This is raw tree access by SHA, used for low-level operations.
    // Unlike zone accessors (which use exportIgnore=true to honor .gitattributes for
    // filtered zone content), this provides unfiltered access to exact tree contents.
    GitAccessorOptions opts{.exportIgnore = false, .smudgeLfs = false};
    auto accessor = repo->getAccessor(hash, opts, "world-tree");

    auto storePath = fetchToStore(
        state.fetchSettings,
        *state.store,
        SourcePath(accessor, CanonPath::root),
        FetchMode::Copy,
        "world-tree-" + std::string(treeSha).substr(0, 12));

    state.allowAndSetStorePathString(storePath, v);
}

static RegisterPrimOp primop_unsafeTectonixInternalTree({
    .name = "__unsafeTectonixInternalTree",
    .args = {"treeSha"},
    .doc = R"(
      Fetch a git tree by SHA from the world repository and return it as a store path.

      Example: `builtins.unsafeTectonixInternalTree "abc123..."` returns `/nix/store/...-world-tree-abc123`.

      Uses `--tectonix-git-dir` (defaults to `~/world/git`).
    )",
    .fun = prim_unsafeTectonixInternalTree,
});

// ============================================================================
// builtins.unsafeTectonixInternalZoneSrc zonePath
// Returns a store path containing the zone source
// With lazy-trees enabled, returns a virtual store path that is only
// materialized when used as a derivation input.
// ============================================================================
static void prim_unsafeTectonixInternalZoneSrc(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto zonePath = state.forceStringNoCtx(*args[0], pos,
        "while evaluating the 'zonePath' argument to builtins.unsafeTectonixInternalZoneSrc");

    validateZonePath(state, pos, zonePath);

    auto storePath = state.getZoneStorePath(zonePath);
    state.allowAndSetStorePathString(storePath, v);
}

static RegisterPrimOp primop_unsafeTectonixInternalZoneSrc({
    .name = "__unsafeTectonixInternalZoneSrc",
    .args = {"zonePath"},
    .doc = R"(
      Get the source of a zone as a store path.

      With `lazy-trees = true`, returns a virtual store path that is only
      materialized when used as a derivation input (devirtualized).

      In source-available mode with uncommitted changes, uses checkout content
      (always eager for dirty zones).

      Example: `builtins.unsafeTectonixInternalZoneSrc "//areas/tools/tec"`

      Uses `--tectonix-git-dir` (defaults to `~/world/git`) and requires
      `--tectonix-git-sha` to be set.
    )",
    .fun = prim_unsafeTectonixInternalZoneSrc,
});

// ============================================================================
// builtins.unsafeTectonixInternalSparseCheckoutRoots
// Returns list of zone IDs in sparse checkout
// ============================================================================
static void prim_unsafeTectonixInternalSparseCheckoutRoots(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto & roots = state.getTectonixSparseCheckoutRoots();

    auto list = state.buildList(roots.size());
    size_t i = 0;
    for (const auto & root : roots) {
        (list[i++] = state.allocValue())->mkString(root, state.mem);
    }
    v.mkList(list);
}

static RegisterPrimOp primop_unsafeTectonixInternalSparseCheckoutRoots({
    .name = "__unsafeTectonixInternalSparseCheckoutRoots",
    .args = {},
    .doc = R"(
      Get the list of zone IDs that are in the sparse checkout.

      Returns an empty list if not in source-available mode or if no
      sparse-checkout-roots file exists.

      Example: `builtins.unsafeTectonixInternalSparseCheckoutRoots` returns `["W-000000" "W-1337af" ...]`.

      Requires `--tectonix-checkout-path` to be set.
    )",
    .fun = prim_unsafeTectonixInternalSparseCheckoutRoots,
});

// ============================================================================
// builtins.unsafeTectonixInternalDirtyZones
// Returns map of zone paths to dirty status
// ============================================================================
static void prim_unsafeTectonixInternalDirtyZones(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto & dirtyZones = state.getTectonixDirtyZones();

    auto attrs = state.buildBindings(dirtyZones.size());
    for (const auto & [zonePath, info] : dirtyZones) {
        attrs.alloc(state.symbols.create(zonePath)).mkBool(info.dirty);
    }
    v.mkAttrs(attrs);
}

static RegisterPrimOp primop_unsafeTectonixInternalDirtyZones({
    .name = "__unsafeTectonixInternalDirtyZones",
    .args = {},
    .doc = R"(
      Get the dirty status of zones in the sparse checkout.

      Returns an attrset mapping zone paths to booleans indicating whether
      the zone has uncommitted changes.

      Only includes zones that are in the sparse checkout.

      Example: `builtins.unsafeTectonixInternalDirtyZones."//areas/tools/dev"` returns `true` or `false`.

      Requires `--tectonix-checkout-path` to be set.
    )",
    .fun = prim_unsafeTectonixInternalDirtyZones,
});

// ============================================================================
// builtins.__unsafeTectonixInternalZoneIsDirty zonePath
// Returns whether a given zone is dirty in the checkout
// ============================================================================
static void prim_unsafeTectonixInternalZoneIsDirty(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto zonePath = state.forceStringNoCtx(*args[0], pos,
        "while evaluating the 'zonePath' argument to builtins.__unsafeTectonixInternalZoneIsDirty");

    validateZonePath(state, pos, zonePath);

    bool isDirty = false;
    if (state.isTectonixSourceAvailable()) {
        auto & dirtyZones = state.getTectonixDirtyZones();
        auto it = dirtyZones.find(std::string(zonePath));
        isDirty = it != dirtyZones.end() && it->second.dirty;
    }

    v.mkBool(isDirty);
}

static RegisterPrimOp primop_unsafeTectonixInternalZoneIsDirty({
    .name = "__unsafeTectonixInternalZoneIsDirty",
    .args = {"zonePath"},
    .doc = R"(
      Get whether a zone is in the sparse checkout and whether it is dirty.

      Example: `builtins.unsafeTectonixInternalZoneIsDirty "//areas/tools/tec"`

      Uses `--tectonix-git-dir` (defaults to `~/world/git`).
    )",
    .fun = prim_unsafeTectonixInternalZoneIsDirty,
});

// ============================================================================
// builtins.__unsafeTectonixInternalZoneRoot zonePath
// Returns an zone root path in sparse checkout
// ============================================================================
static void prim_unsafeTectonixInternalZoneRoot(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto zonePath = state.forceStringNoCtx(*args[0], pos,
        "while evaluating the 'zonePath' argument to builtins.__unsafeTectonixInternalZoneRoot");

    validateZonePath(state, pos, zonePath);

    std::string zone(zonePath);
    if (hasPrefix(zone, "//"))
        zone = zone.substr(2);

    auto checkoutPath = state.settings.tectonixCheckoutPath.get();
    auto fullPath = std::filesystem::path(checkoutPath) / zone;

    if (std::filesystem::exists(fullPath) && !state.settings.pureEval) {
        v.mkString(fullPath.string(), state.mem);
    } else {
        // Zone not accessible in checkout
        v.mkNull();
    }
}

static RegisterPrimOp primop_unsafeTectonixInternalZoneRoot({
    .name = "__unsafeTectonixInternalZoneRoot",
    .args = {"zonePath"},
    .doc = R"(
      Get the root of a zone in sparse checkout, if available.

      With `lazy-trees = true`, returns a virtual store path that is only
      materialized when used as a derivation input (devirtualized).

      In source-available mode with uncommitted changes, uses checkout content
      (always eager for dirty zones).

      Example: `builtins.unsafeTectonixInternalZoneRoot "//areas/tools/tec"`

      Uses `--tectonix-git-dir` (defaults to `~/world/git`).
    )",
    .fun = prim_unsafeTectonixInternalZoneRoot,
});

} // namespace nix
