# Plan: Store Derivations in SQLite Database

## Context

Currently, derivations in LocalStore are stored as `.drv` files on the filesystem at their store path (e.g., `/nix/store/<hash>-foo.drv`). The derivation content (ATerm format) is written via `addToStoreFromDump`, and read back via filesystem accessors.

This plan adds an experimental feature and store setting to store derivation content in the SQLite database instead, eliminating the need for `.drv` files on disk.

The prior art for how this should be done is the `DummyStore`.
That store already separates derivations out from everything else. The same method "intercepting" that is done in that store will also need to be done for the local store.

## Files to Modify

1. **`src/libutil/include/nix/util/experimental-features.hh`** - Add enum value
2. **`src/libutil/experimental-features.cc`** - Add feature metadata
3. **`src/libstore/include/nix/store/local-store.hh`** - Add setting, the experimental feature will solely be used for the setting. The setting will control migrations.
     new DB statements, method overrides may also be needed tobe added here.
4. **`src/libstore/local-store.cc`** - Implementation: schema migration, write/read overrides, statement prep
5. **`src/libstore/local-fs-store.cc`** - Augment `getFSAccessor` for DB-stored derivations
6. New file: **`src/libstore/drv-in-db-schema.sql`** - Schema for new table
7. **`src/libstore/meson.build`** - Add new .sql file to build

## Implementation Steps

### Step 0: `DummyStore` forgot to `Derivation::checkInvariants`

Should always do this before storing a derivation, I think. Local store did this already. Show have some unit tests about adding an invalid derivation.
We can test that in dummy store unit tests too.
Make sure to cover each code path.

### Step 1: Add Experimental Feature `DerivationsInDB`

**`src/libutil/include/nix/util/experimental-features.hh`** (line 41):
```cpp
    BLAKE3Hashes,
    DerivationsInDB,  // NEW
};
```

**`src/libutil/experimental-features.cc`**:
- Update `numXpFeatures` to reference `Xp::DerivationsInDB`
- Add metadata entry at end of `xpFeatureDetails` array:
  ```cpp
  {
      .tag = Xp::DerivationsInDB,
      .name = "derivations-in-db",
      .description = R"(
          Allow storing derivation content in the local store's SQLite
          database instead of as `.drv` files on the filesystem.
      )",
      .trackingUrl = "...",
  },
  ```

### Step 2: Add LocalStore Setting

**`src/libstore/include/nix/store/local-store.hh`** in `LocalStoreConfig`:
```cpp
Setting<bool> drvInDB{
    this,
    false,
    "drv-in-db",
    R"(
      Store derivation content in the SQLite database instead of as
      `.drv` files in the store directory.
    )",
    {},
    true,
    Xp::DerivationsInDB,
};
```

### Step 3: Add Database Schema (Normalized)

New file **`src/libstore/drv-in-db-schema.sql`** with fully normalized tables.
Maps to `BasicDerivation` / `Derivation` fields in `derivations.hh`.

```sql
-- Core derivation metadata (1:1 with ValidPaths for .drv store paths)
create table if not exists Derivations (
    -- FK to ValidPaths(id)
    id                 integer primary key not null,
    -- FK to ValidPaths(path); must be a .drv path
    path               text unique not null
        check (path like '%.drv'),
    -- BasicDerivation::platform
    platform           text not null,
    -- BasicDerivation::builder
    builder            text not null,
    -- BasicDerivation::args
    args               jsonb not null default '[]'
        check (json_type(args) = 'array'),
    -- DerivationOutput variant type (applies to all outputs):
    --   0 = inputAddressed  (outputs have path)
    --   1 = caFixed         (outputs have method, hashAlgo, hash)
    --   2 = caFloating      (outputs have method, hashAlgo)
    --   3 = deferred        (outputs have no extra fields)
    --   4 = impure          (outputs have method, hashAlgo)
    outputType         integer not null
        check (outputType between 0 and 4),
    -- 0 = nullopt, 1 = Some({...})
    -- Distinguishes "no structured attrs" from "empty structured attrs object"
    hasStructuredAttrs integer not null default 0
        check (hasStructuredAttrs in (0, 1)),
    unique (id, outputType),
    unique (id, hasStructuredAttrs),
    foreign key (id) references ValidPaths(id) on delete cascade,
    foreign key (path) references ValidPaths(path) on delete cascade
);

-- BasicDerivation::outputs (DerivationOutput variant)
-- outputType is denormalized from Derivations via composite FK,
-- so per-row CHECK constraints can enforce the right columns per type.
create table if not exists DerivationOutputsInDB (
    drv        integer not null,
    -- output name, e.g. "out"
    id         text not null,
    -- must match parent Derivations.outputType (composite FK enforced)
    outputType integer not null
        check (outputType between 0 and 4),
    -- store path (inputAddressed only)
    path       text,
    -- ContentAddressMethod (caFixed/caFloating/impure)
    method     text,
    -- HashAlgorithm (caFixed/caFloating/impure)
    hashAlgo   text,
    -- hash value (caFixed only)
    hash       text,
    primary key (drv, id),
    foreign key (drv, outputType) references Derivations(id, outputType) on delete cascade,
    -- Enforce exactly one valid column shape per output type:
    check (
        (outputType = 0 -- inputAddressed
            and path is not null
            and method is null and hashAlgo is null and hash is null)
        or (outputType = 1 -- caFixed
            and path is null
            and method is not null and hashAlgo is not null and hash is not null)
        or (outputType = 2 -- caFloating
            and path is null and hash is null
            and method is not null and hashAlgo is not null)
        or (outputType = 3 -- deferred
            and path is null and method is null and hashAlgo is null and hash is null)
        or (outputType = 4 -- impure
            and path is null and hash is null
            and method is not null and hashAlgo is not null)
    )
);

-- Derivation::inputDrvs (top-level DerivedPathMap entries)
--
-- This encode a member of the set of inputs which is a SingleDerivedPath.
create table if not exists DerivationInputs (
    drv        integer not null,
    -- store path of the input
    path      text not null,
    -- JSON array of output names needed from this input
    --
    -- []: Opaque path
    -- [output0]: Built(Opaque path, output0)
    -- [output0, output1]: Built(Built(Opaque path, output0), output1)
    -- [..., outputN]: Built(..., outputN)
    outputs    jsonb not null
        check (json_type(outputs) = 'array'),
    primary key (drv, path),
    foreign key (drv) references Derivations(id) on delete cascade
);

-- BasicDerivation::inputSrcs
create table if not exists DerivationInputSrcs (
    drv        integer not null,
    -- store path
    src        text not null,
    primary key (drv, src),
    foreign key (drv) references Derivations(id) on delete cascade
);

-- BasicDerivation::env
create table if not exists DerivationEnv (
    drv        integer not null,
    key        text not null,
    value      text not null,
    primary key (drv, key),
    foreign key (drv) references Derivations(id) on delete cascade
);

-- BasicDerivation::structuredAttrs (key -> JSON value pairs)
-- Rows can only exist when parent Derivations.hasStructuredAttrs = 1.
-- nullopt vs empty object: if hasStructuredAttrs = 0, no rows possible
-- (FK constraint). If 1, zero rows = empty object {}.
create table if not exists DerivationStructuredAttrs (
    drv                integer not null,
    -- Always 1; enforced by CHECK. Part of composite FK to ensure
    -- parent Derivations row has hasStructuredAttrs = 1.
    hasStructuredAttrs integer not null check (hasStructuredAttrs = 1),
    key                text not null,
    value              jsonb not null,
    primary key (drv, hasStructuredAttrs, key),
    foreign key (drv, hasStructuredAttrs)
        references Derivations(id, hasStructuredAttrs) on delete cascade
);
```

**Notes:**
- The existing `DerivationOutputs` table (in `schema.sql`) stays as-is for backward compat; we add `DerivationOutputsInDB` which captures the full output variant type.
- `DerivationInputDrvs` flattens the top-level `DerivedPathMap` — for dynamic derivations with nested maps (outputs-of-outputs), we'll need to extend this later.
- `DerivationStructuredAttrs` normalizes the JSON object into key → JSON-value pairs.
- All child tables cascade-delete when the ValidPaths entry is removed.

Add to **`src/libstore/meson.build`** alongside `schema.sql` and `ca-specific-schema.sql` for header generation.

### Step 4: Schema Migration + Statement Preparation

In **`local-store.cc`** `upgradeDBSchema()` (following the `ca-derivations` migration pattern):
```cpp
if (config->drvInDB)
    doUpgrade(
        "20260312-derivations-in-db",
#include "drv-in-db-schema.sql.gen.hh"
    );
```

Add new prepared statements to `State::Stmts`:
- `InsertDerivation`: insert into `Derivations` core table
- `InsertDerivationOutput`: insert into `DerivationOutputsInDB`
- `InsertDerivationInputDrv`: insert into `DerivationInputDrvs`
- `InsertDerivationInputSrc`: insert into `DerivationInputSrcs`
- `InsertDerivationEnv`: insert into `DerivationEnv`
- `QueryDerivation`: select from `Derivations` by ValidPaths path
- `QueryDerivationOutputsInDB`: select from `DerivationOutputsInDB` by drv id
- `QueryDerivationInputDrvs`: select from `DerivationInputDrvs` by drv id
- `QueryDerivationInputSrcs`: select from `DerivationInputSrcs` by drv id
- `QueryDerivationEnv`: select from `DerivationEnv` by drv id
- `InsertDerivationStructuredAttr`: insert into `DerivationStructuredAttrs`
- `QueryDerivationStructuredAttrs`: select from `DerivationStructuredAttrs` by drv id

Prepare them conditionally on `config->drvInDB` (same pattern as CA derivation statements).

### Step 5: Override `writeDerivation` in LocalStore

When `config->drvInDB` is true, override `writeDerivation` to:

1. Compute the store path using existing `infoForDerivation()` (reuse from `derivations.cc:129`)
2. `addTempRoot(path)`
3. Check `isValidPath(path) && !repair` for early return
4. `checkInvariants` on the derivation (matches DummyStore pattern)
5. Compute NAR hash of the ATerm content (SHA256 of NAR wrapping the flat file) — still needed for ValidPathInfo
6. Create `ValidPathInfo` from the content address
7. Insert into `ValidPaths` (get the row ID)
8. Decompose the `Derivation` into the normalized tables:
   - Insert into `Derivations` (platform, builder, name)
   - Insert each output into `DerivationOutputsInDB` (with type discriminant)
   - Insert each input drv + output name into `DerivationInputDrvs`
   - Insert each input source into `DerivationInputSrcs`
   - Insert each arg (with index) into `DerivationArgs`
   - Insert each env key/value into `DerivationEnv`
   - Insert each structured attr key/JSON-value into `DerivationStructuredAttrs`
   - Also insert into existing `DerivationOutputs` for GC compat (same as current `cacheDrvOutputMapping`)
9. Update path info cache

When `config->drvInDB` is false, fall through to the default `Store::writeDerivation`.

### Step 6: Override `readDerivation` / `readInvalidDerivation` in LocalStore

Add overrides in `LocalStore`:

```cpp
Derivation LocalStore::readDerivation(const StorePath & drvPath) override;
Derivation LocalStore::readInvalidDerivation(const StorePath & drvPath) override;
```

Implementation:
1. If `config->drvInDB`, look up the ValidPaths id for the path
2. Query `Derivations` table for core fields (platform, builder, name)
3. If found, reconstruct the full `Derivation` from the normalized tables:
   - Query `DerivationOutputsInDB` → reconstruct `DerivationOutput` variants based on `outputType`
   - Query `DerivationInputDrvs` → reconstruct `inputDrvs` DerivedPathMap
   - Query `DerivationInputSrcs` → reconstruct `inputSrcs`
   - Query `DerivationArgs` (ORDER BY idx) → reconstruct `args`
   - Query `DerivationEnv` → reconstruct `env`
   - Query `DerivationStructuredAttrs` → reconstruct `structuredAttrs` JSON object
4. If not found in DB (e.g., derivation was written before the feature was enabled), fall through to the base `Store::readDerivation` (filesystem path)

This ensures backward compatibility: old .drv files on disk still work, new ones go to DB.

### Step 7: Augment `getFSAccessor` for DB-stored Derivations

In **`local-fs-store.cc`**, the per-path `getFSAccessor(const StorePath &, bool)` currently returns a `PosixSourceAccessor` pointing at the filesystem path. For DB-stored derivations, there's no file on disk.

Override in `LocalStore` (or adjust `LocalFSStore`):
- If `config->drvInDB` and the path `isDerivation()`:
  - Read the derivation from DB via `readDerivation` (Step 6)
  - Serialize back to ATerm via `drv.unparse(*this, false)`
  - Return a `MemorySourceAccessor` with the ATerm text as a regular file (same pattern as `DummyStoreImpl::getMemoryFSAccessor`, line 353 of `dummy-store.cc`)
  - If not found, fall through to filesystem check

This makes `narFromPath`, `nix copy`, and other accessor-based operations work transparently.

### Step 8: Handle `addValidPath` Flow

In `addValidPath` (local-store.cc:701), when a path `isDerivation()`, it calls `readInvalidDerivation(info.path)`. With our Step 6 override, this will read from DB if available.

**Ordering concern**: We need the content in the `Derivations` table BEFORE `registerValidPath` is called. In `writeDerivation`, we insert into `Derivations` before calling `registerValidPath`. This works because both happen within the same SQLite transaction context.

Actually, there's a subtlety: `addValidPath` inserts into `ValidPaths` first (getting the row ID), then calls `readInvalidDerivation`. But our `InsertDerivation` in `writeDerivation` needs the `ValidPaths.id`, which doesn't exist yet.

**Solution**: Change `addValidPath` to accept an optional pre-parsed `Derivation` when available, avoiding the need to re-read. When called from our `writeDerivation` override, pass the derivation we already have. Store the content in `Derivations` table using the freshly-obtained `ValidPaths.id` from `addValidPath`.

Concretely:
- In `writeDerivation` override, call a new method like `addValidPathWithDrv(state, info, drv, contents)` that:
  1. Inserts into ValidPaths (gets ID)
  2. Inserts derivation content into Derivations table using that ID
  3. Checks invariants and caches output mappings (using the passed-in `drv` rather than re-reading)

### Step 9: Handle Garbage Collection

In `deleteStorePath` and related GC code, when deleting a DB-stored derivation:
- The `Derivations` table has `ON DELETE CASCADE` from `ValidPaths`, so invalidating the path automatically removes the derivation content
- If no `.drv` file exists on disk, `deleteStorePath` should gracefully handle the missing file (it likely already does since it calls `deletePath` which handles ENOENT)

## Verification

1. **Build**: `ninja all -j20 -k0` - ensure it compiles
2. **Unit tests**: `meson test nix-store-tests` - existing tests should pass (feature disabled by default)
3. **Manual test with feature enabled**:
   ```bash
   ./build/src/nix/nix --extra-experimental-features derivations-in-db \
     --store 'path-to-temp-store?drv-in-db=true' \
     build --expr '(import <nixpkgs> {}).hello' --dry-run
   ```
   Then verify the derivation can be read back, and no `.drv` file exists in the store.
4. **Backward compat**: Ensure existing stores (with .drv files on disk) continue to work with the feature enabled (reads fall through to filesystem).
5. **Run functional tests**: `meson test` to check nothing is broken when feature is disabled.
