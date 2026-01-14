# Tectonix Lazy Trees Integration Plan

> **Implementation Status:** This plan has been implemented with modifications. Key differences from the original plan:
> - `worldRoot` builtin was **not implemented** (the zone-based approach was deemed sufficient)
> - Zone path validation was added to ensure only exact zone roots can be accessed
> - Thread-safe caching was implemented using `std::call_once` for all lazy-init fields
> - Lazy mounting for dirty zones from checkout was added (not in original plan)
> - `prim_worldZone` became `__unsafeTectonixInternalZone` (internal API)
> - Zone names are sanitized for store path requirements (replacing invalid chars)
> - `allowPath()` is called after `mount()` to prevent resource leaks on exception
> - Git status uses `-z` flag for NUL-separated output to handle special characters
> - Debug logging was added to key functions (`getWorldTreeSha`, `getManifestContent`, etc.)
> - See actual implementation in `src/libexpr/primops/tectonix.cc` and `src/libexpr/eval.cc`

This document outlines the plan to integrate tectonix zone access with Nix's lazy-trees infrastructure, enabling on-demand copying of zone sources to the store.

## Background

### Current Behavior

When `builtins.unsafeTectonixInternalZoneSrc "//areas/tools/tec"` is called, the entire zone is immediately copied to the Nix store via `fetchToStore()`, regardless of whether the zone content is actually needed for a derivation.

### Lazy Trees in Flakes

With `lazy-trees = true`, flakes avoid this eager copying:

1. `mountInput()` creates a random store path and mounts a `GitSourceAccessor` at that path
2. Files are read on-demand from the git ODB during evaluation
3. Only when the path is used as a derivation input does `devirtualize()` copy it to the store

### Goal

Apply the same lazy behavior to tectonix zones, while respecting zone boundaries and dirty zone detection.

---

## Architectural Comparison: Flakes vs Tectonix

### Flakes

```
FlakeRef (github:nixos/nixpkgs/abc123)
    │
    ▼
InputCache.getAccessor()
    │
    ▼
Input.getAccessor() → GitSourceAccessor (lazy)
    │
    ▼
mountInput()
    │
    ├─► lazyTrees=false: fetchToStore() immediately
    │
    └─► lazyTrees=true:
            StorePath::random("nixpkgs")
            storeFS->mount(storePath, accessor)
            return virtual path

Later, when used in derivation:
    │
    ▼
devirtualize() → fetchToStore() → real store path
```

**Key point:** Each flake is its own unit. The accessor is rooted at the flake, and the whole flake gets mounted at one store path.

### Tectonix Challenge

```
world @ sha:abc123
├── areas/
│   ├── tools/
│   │   ├── tec/          ← Zone (tree: deadbeef)
│   │   ├── dev/          ← Zone (tree: cafebabe)
│   │   └── ...
│   └── platform/
│       └── ...
└── .meta/
    └── manifest.json

Problem: Can't mount whole world at one path!

Using /nix/store/xxx-world/areas/tools/tec as derivation src
would pull in the ENTIRE world when devirtualized.

Solution: Mount each zone separately at its own store path.
```

### What Makes Tectonix Harder

1. **Granularity mismatch**: Flakes = one input = one mount. World = one repo = thousands of zones.
2. **No `Input` abstraction**: Flakes have `fetchers::Input` with `getAccessor()`, caching, locking. Tectonix builtins are ad-hoc.
3. **Dirty zone complexity**: Flakes mark dirty inputs as "unlocked". Tectonix needs zone-granular dirty detection with checkout fallback.
4. **Two-mode operation**: Git ODB vs checkout. Flakes only have one source per input.

### What Makes Tectonix Easier

1. **Content-addressed by nature**: Tree SHA is the *perfect* cache key. Same tree SHA across different world commits = identical content.
2. **No resolution complexity**: No registries, no indirect references, no lock file management.
3. **Already have the accessor**: `getWorldGitAccessor()` returns a lazy `GitSourceAccessor`.
4. **Single source of truth**: One repo, one commit SHA.

---

## Design

### Core Concept: Zone Mounts by Tree SHA

```
builtins.worldZone "//areas/tools/tec"
    │
    ▼
getZoneStorePath(zonePath)
    │
    ├─► isDirty? ─────────────────────────────┐
    │       │                                  │
    │       ▼                                  │
    │   getZoneFromCheckout()                  │
    │   (EXTENSION POINT: eager for now)       │
    │       │                                  │
    │       ▼                                  │
    │   return store path ◄────────────────────┘
    │
    └─► !isDirty
            │
            ▼
        treeSha = getWorldTreeSha(zonePath)
            │
            ▼
        mountZoneByTreeSha(treeSha)
            │
            ├─► cached? return cached store path
            │
            └─► not cached:
                    accessor = repo->getAccessor(treeSha)
                    storePath = StorePath::random(name)
                    storeFS->mount(storePath, accessor)
                    cache[treeSha] = storePath
                    return storePath
```

### Why Tree SHA as Cache Key

```
World @ v1 (sha: aaa)              World @ v2 (sha: bbb)
├── areas/tools/tec                ├── areas/tools/tec
│   (tree: deadbeef)  ─────────────│   (tree: deadbeef)  ← SAME!
│                                  │
├── areas/tools/dev                ├── areas/tools/dev
│   (tree: cafebabe)               │   (tree: 12345678)  ← Changed
```

If `//areas/tools/tec` didn't change between commits, its tree SHA is identical. The zone cache returns the same virtual store path, and when devirtualized, the same real store path. **Natural deduplication across world revisions.**

---

## Implementation

### Phase 1: Core Infrastructure

#### 1.1 EvalState Additions (`src/libexpr/include/nix/expr/eval.hh`)

```cpp
// In EvalState class:

private:
    /**
     * Cache tree SHA → virtual store path for lazy zone mounts.
     * Thread-safe for eval-cores > 1.
     */
    Sync<std::map<Hash, StorePath>> tectonixZoneCache_;

public:
    /**
     * Get a zone's store path, handling dirty detection and lazy mounting.
     *
     * For clean zones with lazy-trees enabled: mounts accessor lazily
     * For dirty zones: currently eager-copies from checkout (extension point)
     * For lazy-trees disabled: eager-copies from git
     */
    StorePath getZoneStorePath(std::string_view zonePath);

private:
    /**
     * Mount a zone by tree SHA, returning a (potentially virtual) store path.
     * Caches by tree SHA for deduplication across world revisions.
     */
    StorePath mountZoneByTreeSha(const Hash & treeSha, std::string_view zonePath);

    /**
     * Get zone store path from checkout (for dirty zones).
     * EXTENSION POINT: Currently always eager. Could be made lazy later.
     */
    StorePath getZoneFromCheckout(std::string_view zonePath);
```

#### 1.2 Implementation (`src/libexpr/eval.cc`)

```cpp
StorePath EvalState::getZoneStorePath(std::string_view zonePath)
{
    // Normalize path
    std::string zone(zonePath);
    if (hasPrefix(zone, "//"))
        zone = zone.substr(2);

    // Check dirty status
    bool isDirty = false;
    if (isTectonixSourceAvailable()) {
        auto & dirtyZones = getTectonixDirtyZones();
        auto it = dirtyZones.find(std::string(zonePath));
        isDirty = it != dirtyZones.end() && it->second;
    }

    if (isDirty) {
        // EXTENSION POINT: For now, always eager from checkout
        return getZoneFromCheckout(zonePath);
    }

    // Clean zone: get tree SHA
    auto treeSha = getWorldTreeSha(zonePath);

    if (!settings.lazyTrees) {
        // Eager mode: immediate copy from git ODB
        auto repo = getWorldRepo();
        GitAccessorOptions opts{.exportIgnore = true, .smudgeLfs = false};
        auto accessor = repo->getAccessor(treeSha, opts, "zone");

        std::string name = "zone-" + replaceStrings(zone, "/", "-");
        auto storePath = fetchToStore(
            fetchSettings, *store,
            SourcePath(accessor, CanonPath::root),
            FetchMode::Copy, name);

        allowPath(storePath);
        return storePath;
    }

    // Lazy mode: mount by tree SHA
    return mountZoneByTreeSha(treeSha, zonePath);
}

StorePath EvalState::mountZoneByTreeSha(const Hash & treeSha, std::string_view zonePath)
{
    // Check cache first (thread-safe)
    {
        auto cache = tectonixZoneCache_.readLock();
        auto it = cache->find(treeSha);
        if (it != cache->end()) {
            debug("zone cache hit for tree %s", treeSha.gitRev());
            return it->second;
        }
    }

    // Not cached: create accessor and mount
    auto repo = getWorldRepo();
    GitAccessorOptions opts{.exportIgnore = true, .smudgeLfs = false};
    auto accessor = repo->getAccessor(treeSha, opts, "zone");

    // Generate name from zone path
    std::string zone(zonePath);
    if (hasPrefix(zone, "//"))
        zone = zone.substr(2);
    std::string name = "zone-" + replaceStrings(zone, "/", "-");

    // Create virtual store path
    auto storePath = StorePath::random(name);
    allowPath(storePath);

    // Mount accessor at this path
    storeFS->mount(CanonPath(store->printStorePath(storePath)), accessor);

    // Cache it (thread-safe)
    {
        auto cache = tectonixZoneCache_.lock();
        auto [it, inserted] = cache->try_emplace(treeSha, storePath);
        if (!inserted) {
            // Another thread beat us, use their path
            return it->second;
        }
    }

    debug("mounted zone %s (tree %s) at %s",
          zonePath, treeSha.gitRev(), store->printStorePath(storePath));

    return storePath;
}

StorePath EvalState::getZoneFromCheckout(std::string_view zonePath)
{
    // EXTENSION POINT: Currently always eager.
    //
    // To make this lazy later, we'd need to:
    // 1. Create a filtered accessor over the checkout path
    // 2. Compute a content key (hash of modified files? mtime-based?)
    // 3. Cache and mount like mountZoneByTreeSha
    //
    // For now: just copy from checkout.

    std::string zone(zonePath);
    if (hasPrefix(zone, "//"))
        zone = zone.substr(2);

    auto checkoutAccessor = getWorldCheckoutAccessor();
    if (!checkoutAccessor)
        throw Error("checkout accessor not available for dirty zone '%s'", zonePath);

    auto checkoutPath = settings.tectonixCheckoutPath.get();
    auto fullPath = CanonPath(checkoutPath + "/" + zone);

    std::string name = "zone-" + replaceStrings(zone, "/", "-");

    auto storePath = fetchToStore(
        fetchSettings, *store,
        SourcePath(*checkoutAccessor, fullPath),
        FetchMode::Copy, name);

    allowPath(storePath);
    return storePath;
}
```

### Phase 2: Updated Builtins (`src/libexpr/primops/tectonix.cc`)

#### 2.1 Simplify `prim_unsafeTectonixInternalZoneSrc`

```cpp
static void prim_unsafeTectonixInternalZoneSrc(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto zonePath = state.forceStringNoCtx(*args[0], pos,
        "while evaluating the 'zonePath' argument to builtins.unsafeTectonixInternalZoneSrc");

    auto storePath = state.getZoneStorePath(zonePath);
    state.allowAndSetStorePathString(storePath, v);
}
```

#### 2.2 New `prim_worldZone` (flake-like interface)

```cpp
static void prim_worldZone(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto zonePath = state.forceStringNoCtx(*args[0], pos,
        "while evaluating the 'zonePath' argument to builtins.worldZone");

    // Get tree SHA before we potentially fetch
    auto treeSha = state.getWorldTreeSha(zonePath);

    // Check dirty status
    bool isDirty = false;
    if (state.isTectonixSourceAvailable()) {
        auto & dirtyZones = state.getTectonixDirtyZones();
        auto it = dirtyZones.find(std::string(zonePath));
        isDirty = it != dirtyZones.end() && it->second;
    }

    auto storePath = state.getZoneStorePath(zonePath);
    auto storePathStr = state.store->printStorePath(storePath);

    // Build result attrset (like fetchTree)
    auto attrs = state.buildBindings(4);

    attrs.alloc("outPath").mkString(storePathStr, {
        NixStringContextElem::Opaque{storePath}
    });
    attrs.alloc("treeSha").mkString(treeSha.gitRev(), state.mem);
    attrs.alloc("zonePath").mkString(zonePath, state.mem);
    attrs.alloc("dirty").mkBool(isDirty);

    v.mkAttrs(attrs);
}

static RegisterPrimOp primop_worldZone({
    .name = "worldZone",
    .args = {"zonePath"},
    .doc = R"(
      Get a zone from the world repository.

      Returns an attrset with:
      - outPath: Store path containing zone source (lazy with lazy-trees)
      - treeSha: Git tree SHA for this zone
      - zonePath: The zone path argument
      - dirty: Whether the zone has uncommitted changes

      Example: `builtins.worldZone "//areas/tools/tec"`

      Requires `--tectonix-git-dir` and `--tectonix-sha` to be set.
    )",
    .fun = prim_worldZone,
});
```

#### 2.3 New `prim_worldRoot` (read-only world access) — NOT IMPLEMENTED

> **Note:** This builtin was not implemented. The zone-based approach (`__unsafeTectonixInternalZone`)
> was deemed sufficient for all use cases, and `worldRoot` was removed to avoid accidentally copying
> the entire world repository to the store.

```cpp
static void prim_worldRoot(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    // Lazily mount the whole world accessor once per evaluation
    auto storePath = state.getOrMountWorldRoot();

    v.mkPath(state.rootPath(
        CanonPath(state.store->printStorePath(storePath))));
}

static RegisterPrimOp primop_worldRoot({
    .name = "worldRoot",
    .args = {},
    .doc = R"(
      Get a path to the world repository root.

      This path can be used for reading files during evaluation:

          let world = builtins.worldRoot;
          in import (world + "/areas/tools/tec/zone.nix")

      WARNING: Do not use this path directly as a derivation src!
      That would copy the entire world to the store. Use
      builtins.worldZone for derivation sources.

      Requires `--tectonix-git-dir` and `--tectonix-sha` to be set.
    )",
    .fun = prim_worldRoot,
});
```

With supporting method in `EvalState`:

```cpp
StorePath EvalState::getOrMountWorldRoot()
{
    // Thread-safe lazy initialization
    static std::once_flag mounted;
    static StorePath worldStorePath;

    std::call_once(mounted, [this]() {
        auto accessor = getWorldGitAccessor();
        worldStorePath = StorePath::random("world");
        allowPath(worldStorePath);
        storeFS->mount(
            CanonPath(store->printStorePath(worldStorePath)),
            accessor);
    });

    return worldStorePath;
}
```

---

## Usage Examples

### Before (eager)

```nix
let
  zoneSrc = builtins.unsafeTectonixInternalZoneSrc "//areas/tools/tec";
  # ^ Entire zone copied to store immediately
in
mkDerivation {
  src = zoneSrc;
  ...
}
```

### After (lazy)

```nix
let
  world = builtins.worldRoot;

  # Read-only access (no store copy during evaluation)
  zoneNix = import (world + "/areas/tools/tec/zone.nix");
  manifest = builtins.fromJSON (builtins.readFile (world + "/.meta/manifest.json"));

  # For derivation src, use worldZone (zone-granular lazy copy)
  tecZone = builtins.worldZone "//areas/tools/tec";
in
mkDerivation {
  src = tecZone.outPath;  # Only copied when derivation is built
  ...
}
```

---

## Builtin Migration Guide

| Old Pattern | New Pattern |
|-------------|-------------|
| `__unsafeTectonixInternalZoneSrc path` | `(worldZone path).outPath` |
| `__unsafeTectonixInternalTreeSha path` then `__unsafeTectonixInternalTree sha` | `(worldZone path).outPath` |
| `__unsafeTectonixInternalFile path` | `builtins.readFile (worldRoot + path)` |
| `__unsafeTectonixInternalDir zone subpath` | `builtins.readDir (worldRoot + zone + "/" + subpath)` |

The `__unsafeTectonixInternalTree` builtin can be retained for edge cases (fetching arbitrary tree SHAs not corresponding to zones), but becomes less central.

---

## Extension Point: Lazy Dirty Zones

The `getZoneFromCheckout()` function is the clear extension point for future optimization.

### Current Behavior

Dirty zones are always eagerly copied from checkout:

```cpp
StorePath EvalState::getZoneFromCheckout(std::string_view zonePath)
{
    // Always eager for now
    return fetchToStore(...);
}
```

### Future Options

1. **Content-hash dirty files**
   - Walk checkout, hash modified files
   - Use combined hash as cache key
   - Complex but accurate

2. **Overlay accessor**
   - Base: git ODB accessor for zone
   - Overlay: checkout accessor filtered to dirty files
   - Mount the composite accessor
   - Cache key: `(treeSha, set of dirty file paths)`

3. **Mtime-based caching**
   - Use checkout accessor with mtime as cache key
   - Simpler but may re-copy on unrelated file touches

The interface is clean: `getZoneStorePath()` decides dirty vs clean and delegates appropriately. The dirty path can be made lazy without changing callers.

---

## Testing Plan

1. **Lazy-trees enabled, clean zone**
   - Verify virtual store path is created
   - Verify no immediate copy to store
   - Verify devirtualization on derivation build

2. **Lazy-trees disabled**
   - Verify immediate copy (current behavior preserved)

3. **Dirty zones**
   - Verify fallback to checkout
   - Verify eager copy (for now)

4. **Cache behavior**
   - Same tree SHA returns same virtual path
   - Different tree SHA returns different path
   - Thread-safe with `eval-cores > 1`

5. **Cross-world-revision deduplication**
   - Zone unchanged between commits → same devirtualized store path

---

## Summary

| Component | Purpose |
|-----------|---------|
| `tectonixZoneCache_` | Tree SHA → virtual store path mapping |
| `tectonixCheckoutZoneCache_` | Zone path → virtual store path for checkout zones |
| `getZoneStorePath()` | Orchestrator: dirty detection → dispatch |
| `mountZoneByTreeSha()` | Lazy mount for clean zones |
| `getZoneFromCheckout()` | Lazy mount for dirty zones from checkout |
| `__unsafeTectonixInternalZone` | High-level builtin returning attrset |
| `__unsafeTectonixInternalZoneSrc` | Simple builtin returning store path string |
| ~~`worldRoot`~~ | **Not implemented** - zone-based approach deemed sufficient |

This design:
- Integrates cleanly with existing lazy-trees infrastructure
- Uses tree SHA for natural content-addressed caching
- Supports lazy mounting for both clean zones (from git) and dirty zones (from checkout)
- Provides flake-like API consistency via `__unsafeTectonixInternalZone`
