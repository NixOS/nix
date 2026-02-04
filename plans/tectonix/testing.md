# Tectonix Lazy-Trees Testing Plan

This document outlines the testing strategy for the tectonix lazy-trees integration implemented in commit `36c8f88ae`.

## Test Infrastructure Overview

Nix uses two main testing approaches:

1. **C++ Unit Tests** (`src/lib*-tests/*.cc`) - gtest/gmock based, for isolated component testing
2. **Functional Tests** (`tests/functional/*.sh`) - Shell scripts for end-to-end integration

Our testing will use both approaches.

---

## Phase 1: Unit Tests for Helper Functions

**Location:** `src/libexpr-tests/tectonix.cc` (new file)

### 1.1 Path Normalization (`normalizeZonePath`)

Currently a static function in `eval.cc`. Consider exposing via a test-only header or testing indirectly through builtins.

| Test Case | Input | Expected Output |
|-----------|-------|-----------------|
| Strip double-slash prefix | `"//areas/tools/dev"` | `"areas/tools/dev"` |
| No prefix passthrough | `"areas/tools/dev"` | `"areas/tools/dev"` |
| Root path | `"//"` | `""` |
| Single slash (edge case) | `"/areas"` | `"/areas"` |

### 1.2 Store Path Sanitization (`sanitizeZoneNameForStore`)

| Test Case | Input | Expected Output |
|-----------|-------|-----------------|
| Slashes to dashes | `"//areas/tools/dev"` | `"areas-tools-dev"` |
| Valid chars preserved | `"foo-bar.baz_123"` | `"foo-bar.baz_123"` |
| Invalid chars replaced | `"foo@bar#baz"` | `"foo_bar_baz"` |
| Unicode chars replaced | `"föö/bär"` | `"f__-b_r"` |
| Empty after normalization | `"//"` | `""` |

### 1.3 Zone Path Validation (`validateZonePath`)

| Test Case | Behavior |
|-----------|----------|
| Valid zone path in manifest | Returns without error |
| Path not in manifest | Throws `EvalError` with message about "not a zone root" |
| Subpath of zone (not exact match) | Throws error |
| Parent of zone | Throws error |

---

## Phase 2: Git-Utils Unit Tests

**Location:** `src/libfetchers-tests/git-utils.cc` (extend existing)

### 2.1 `odbOnly` Mode

```cpp
TEST_F(GitUtilsTest, odbOnly_opens_repository)
{
    // Create bare repo with objects
    // Open with odbOnly=true
    // Verify can read objects by SHA
}

TEST_F(GitUtilsTest, odbOnly_fails_without_objects_dir)
{
    // Try to open non-existent path with odbOnly=true
    // Should throw appropriate error
}

TEST_F(GitUtilsTest, odbOnly_with_reftables_extension)
{
    // Create repo with extensions.refstorage=reftables in config
    // Verify odbOnly=true can still open it
    // Verify odbOnly=false would fail
}
```

### 2.2 `getSubtreeSha`

```cpp
TEST_F(GitUtilsTest, getSubtreeSha_finds_entry)
{
    // Create tree with subdirectory
    // Verify getSubtreeSha returns correct SHA for subdir
}

TEST_F(GitUtilsTest, getSubtreeSha_missing_entry)
{
    // Request non-existent entry
    // Should throw error mentioning "not found in tree"
}

TEST_F(GitUtilsTest, getSubtreeSha_non_directory)
{
    // Request entry that is a file, not directory
    // Should throw error mentioning "not a directory"
}
```

### 2.3 `getCommitTree`

```cpp
TEST_F(GitUtilsTest, getCommitTree_returns_root_tree)
{
    // Create commit with known tree
    // Verify getCommitTree returns that tree SHA
}

TEST_F(GitUtilsTest, getCommitTree_invalid_sha)
{
    // Pass tree SHA instead of commit SHA
    // Should throw appropriate error
}
```

### 2.4 `readDirectory` with Type Information

```cpp
TEST_F(GitUtilsTest, readDirectory_returns_types)
{
    // Create tree with: file, directory, symlink, submodule
    // Verify readDirectory returns correct type for each
}
```

---

## Phase 3: EvalState Tectonix Methods

**Location:** `src/libexpr-tests/tectonix.cc` (new file)

These tests require a test fixture that sets up:
- A temporary git repository with known structure
- Manifest file at `.meta/manifest.json`
- EvalState configured with `tectonix-git-dir` and `tectonix-git-sha`

### 3.1 Test Fixture

```cpp
class TectonixTest : public LibExprTest
{
protected:
    std::filesystem::path repoPath;
    std::string commitSha;

    void SetUp() override;  // Create repo with zones
    void TearDown() override;  // Cleanup

    // Helper to create EvalState with tectonix settings
    EvalState createTectonixState(bool withCheckout = false);
};
```

### 3.2 `getWorldRepo` Tests

| Test Case | Behavior |
|-----------|----------|
| Valid git dir | Returns repo reference |
| Missing git dir setting | Throws error about `--tectonix-git-dir` |
| Non-existent path | Throws git open error |
| Home expansion (`~`) | Expands correctly |
| Repeated calls | Returns same cached instance |

### 3.3 `getWorldGitAccessor` Tests

| Test Case | Behavior |
|-----------|----------|
| Valid SHA | Returns accessor |
| Missing SHA setting | Throws error about `--tectonix-git-sha` |
| Invalid SHA (not found) | Throws error about SHA not found |
| Tree SHA instead of commit | Throws error about "not a valid commit" |
| Blob SHA instead of commit | Throws error about "not a valid commit" |

### 3.4 `getWorldTreeSha` Tests

| Test Case | Input | Behavior |
|-----------|-------|----------|
| Valid zone path | `"//areas/tools/dev"` | Returns correct tree SHA |
| Nested path | `"//areas/tools/dev/src"` | Returns correct tree SHA |
| Non-existent path | `"//does/not/exist"` | Throws error |
| Path traversal attempt | `"//areas/../secret"` | Throws error about invalid component |
| Root path | `"//"` | Returns root tree SHA |
| Caching | Same path twice | Second call returns cached value |
| Intermediate caching | `"//a/b/c"` then `"//a/b"` | Second uses cached intermediate |

### 3.5 `getManifestContent` / `getManifestJson` Tests

| Test Case | Behavior |
|-----------|----------|
| Manifest in git | Returns content from git |
| Manifest in checkout (source-available) | Prefers checkout over git |
| Missing manifest | Throws error |
| Invalid JSON | `getManifestJson` throws parse error |
| Caching | Multiple calls return same instance |

### 3.6 `getTectonixSparseCheckoutRoots` Tests

| Test Case | Behavior |
|-----------|----------|
| File exists with zone IDs | Returns set of IDs |
| File missing | Returns empty set |
| Worktree `.git` file | Correctly follows gitdir reference |
| Empty file | Returns empty set |
| File with blank lines | Ignores blank lines |

### 3.7 `getTectonixDirtyZones` Tests

| Test Case | Behavior |
|-----------|----------|
| No dirty files | All zones marked clean |
| Modified file in zone | Zone marked dirty |
| New untracked file | Zone marked dirty |
| Deleted file | Zone marked dirty |
| Renamed file | Both source and dest zones marked dirty |
| File outside sparse checkout | Ignored |
| Git status fails | Warning logged, zones treated as clean |

### 3.8 `getZoneStorePath` Tests

| Test Case | Mode | Behavior |
|-----------|------|----------|
| Clean zone, lazy-trees=true | Lazy | Returns virtual store path |
| Clean zone, lazy-trees=false | Eager | Returns real store path (copied) |
| Dirty zone, lazy-trees=true | Lazy checkout | Returns virtual path from checkout |
| Dirty zone, lazy-trees=false | Eager | Returns real store path from checkout |
| Same zone twice | Any | Returns same cached path |
| Different zones, same tree SHA | Lazy | Returns same path (deduplication) |

### 3.9 `mountZoneByTreeSha` Tests

| Test Case | Behavior |
|-----------|----------|
| First mount | Creates accessor, mounts, caches |
| Cache hit | Returns cached path without git access |
| Concurrent mounts (same SHA) | Only one mount created |
| Different SHAs | Different store paths |

### 3.10 `getZoneFromCheckout` Tests

| Test Case | Behavior |
|-----------|----------|
| Valid zone in checkout | Returns store path |
| Zone not in checkout | Throws error |
| Lazy mode | Mounts live filesystem |
| Eager mode | Copies to store |

---

## Phase 4: Builtin (Primop) Tests

**Location:** `src/libexpr-tests/tectonix.cc`

Extend the `TectonixTest` fixture to test all builtins.

### 4.1 `__unsafeTectonixInternalManifest`

```cpp
TEST_F(TectonixTest, manifest_returns_path_to_metadata_mapping)
{
    auto v = eval("builtins.__unsafeTectonixInternalManifest");
    ASSERT_THAT(v, IsAttrs());
    // Verify known zone paths map to correct IDs via .id
}

TEST_F(TectonixTest, manifest_missing_id_field)
{
    // Set up manifest with missing "id" field
    // Should throw error
}
```

### 4.2 `__unsafeTectonixInternalManifestInverted`

```cpp
TEST_F(TectonixTest, manifest_inverted_returns_id_to_path_mapping)
{
    auto v = eval("builtins.__unsafeTectonixInternalManifestInverted");
    ASSERT_THAT(v, IsAttrs());
    // Verify known IDs map to correct paths
}

TEST_F(TectonixTest, manifest_inverted_duplicate_id)
{
    // Set up manifest with duplicate IDs
    // Should throw error about duplicate
}
```

### 4.3 `__unsafeTectonixInternalTreeSha`

```cpp
TEST_F(TectonixTest, treeSha_returns_correct_sha)
{
    auto v = eval("builtins.__unsafeTectonixInternalTreeSha \"//areas/tools/dev\"");
    ASSERT_THAT(v, IsString());
    // Verify SHA matches expected value
}

TEST_F(TectonixTest, treeSha_invalid_path)
{
    ASSERT_THROW(
        eval("builtins.__unsafeTectonixInternalTreeSha \"//invalid\""),
        Error);
}
```

### 4.4 `__unsafeTectonixInternalTree`

```cpp
TEST_F(TectonixTest, tree_fetches_by_sha)
{
    // Get a known tree SHA
    // Verify __unsafeTectonixInternalTree returns store path
    // Verify contents match expected
}

TEST_F(TectonixTest, tree_invalid_sha)
{
    ASSERT_THROW(
        eval("builtins.__unsafeTectonixInternalTree \"0000000000000000000000000000000000000000\""),
        EvalError);
}
```

### 4.5 `__unsafeTectonixInternalZoneSrc`

```cpp
TEST_F(TectonixTest, zoneSrc_returns_store_path)
{
    auto v = eval("builtins.__unsafeTectonixInternalZoneSrc \"//areas/tools/dev\"");
    ASSERT_THAT(v, IsString());
    // Verify path starts with store prefix
}

TEST_F(TectonixTest, zoneSrc_validates_zone_path)
{
    // Try to access subpath of zone (not exact zone root)
    ASSERT_THROW(
        eval("builtins.__unsafeTectonixInternalZoneSrc \"//areas/tools/dev/subdir\""),
        EvalError);
}

TEST_F(TectonixTest, zoneSrc_has_context)
{
    auto v = eval("builtins.__unsafeTectonixInternalZoneSrc \"//areas/tools/dev\"");
    // Verify string has store path context
}
```

### 4.6 `__unsafeTectonixInternalZone`

```cpp
TEST_F(TectonixTest, zone_returns_attrset)
{
    auto v = eval("builtins.__unsafeTectonixInternalZone \"//areas/tools/dev\"");
    ASSERT_THAT(v, IsAttrsOfSize(5));

    // Check outPath
    auto outPath = v.attrs()->get(createSymbol("outPath"));
    ASSERT_NE(outPath, nullptr);
    ASSERT_THAT(*outPath->value, IsString());

    // Check root
    auto root = v.attrs()->get(createSymbol("root"));
    ASSERT_NE(root, nullptr);
    // root should be a path type

    // Check treeSha
    auto treeSha = v.attrs()->get(createSymbol("treeSha"));
    ASSERT_NE(treeSha, nullptr);
    ASSERT_THAT(*treeSha->value, IsString());

    // Check zonePath
    auto zonePath = v.attrs()->get(createSymbol("zonePath"));
    ASSERT_NE(zonePath, nullptr);
    ASSERT_THAT(*zonePath->value, IsStringEq("//areas/tools/dev"));

    // Check dirty
    auto dirty = v.attrs()->get(createSymbol("dirty"));
    ASSERT_NE(dirty, nullptr);
    ASSERT_THAT(*dirty->value, IsFalse());  // clean zone
}

TEST_F(TectonixTest, zone_dirty_flag_true_for_modified)
{
    // Modify a file in checkout
    // Verify dirty=true
}

TEST_F(TectonixTest, zone_root_can_read_files)
{
    // Use root to import a nix file without triggering store copy
    auto v = eval(R"(
        let zone = builtins.__unsafeTectonixInternalZone "//areas/tools/dev";
        in builtins.readFile (zone.root + "/zone.nix")
    )");
    ASSERT_THAT(v, IsString());
}

TEST_F(TectonixTest, zone_outPath_has_context)
{
    auto v = eval(R"(
        (builtins.__unsafeTectonixInternalZone "//areas/tools/dev").outPath
    )");
    // Verify string context includes store path
}
```

### 4.7 `__unsafeTectonixInternalSparseCheckoutRoots`

```cpp
TEST_F(TectonixTest, sparseCheckoutRoots_returns_list)
{
    auto v = eval("builtins.__unsafeTectonixInternalSparseCheckoutRoots");
    ASSERT_THAT(v, IsList());
}

TEST_F(TectonixTest, sparseCheckoutRoots_empty_without_checkout)
{
    // Configure without checkout path
    auto v = eval("builtins.__unsafeTectonixInternalSparseCheckoutRoots");
    ASSERT_THAT(v, IsListOfSize(0));
}
```

### 4.8 `__unsafeTectonixInternalDirtyZones`

```cpp
TEST_F(TectonixTest, dirtyZones_returns_attrset)
{
    auto v = eval("builtins.__unsafeTectonixInternalDirtyZones");
    ASSERT_THAT(v, IsAttrs());
}

TEST_F(TectonixTest, dirtyZones_values_are_booleans)
{
    auto v = eval("builtins.__unsafeTectonixInternalDirtyZones");
    // Iterate attrs, verify all values are bools
}
```

---

## Phase 5: Thread-Safety Tests

**Location:** `src/libexpr-tests/tectonix.cc`

### 5.1 Concurrent Zone Access

```cpp
TEST_F(TectonixTest, concurrent_zone_mounts)
{
    // Launch multiple threads calling getZoneStorePath for same zone
    // Verify all get same path
    // Verify only one actual mount occurred
}

TEST_F(TectonixTest, concurrent_different_zones)
{
    // Launch multiple threads accessing different zones
    // Verify no deadlocks
    // Verify each gets correct path
}

TEST_F(TectonixTest, concurrent_tree_sha_computation)
{
    // Launch multiple threads computing tree SHAs for overlapping paths
    // Verify cache is populated correctly
    // Verify no races in intermediate caching
}
```

### 5.2 Lazy Init Thread Safety

```cpp
TEST_F(TectonixTest, concurrent_manifest_access)
{
    // Multiple threads calling getManifestJson
    // Verify all get same instance
}

TEST_F(TectonixTest, concurrent_dirty_zone_detection)
{
    // Multiple threads calling getTectonixDirtyZones
    // Verify consistent results
}
```

---

## Phase 6: Functional Tests

**Location:** `tests/functional/tectonix/` (new directory)

### 6.1 Setup Scripts

Create `tests/functional/tectonix/common.sh`:

```bash
# Create test world repository with:
# - .meta/manifest.json
# - //areas/tools/dev/ (zone with zone.nix)
# - //areas/tools/tec/ (another zone)
# - //areas/platform/core/ (zone with dependencies)

create_test_world() {
    local dir="$1"
    # ... setup git repo with zones
}
```

### 6.2 Test Cases

**`tests/functional/tectonix/basic.sh`:**
```bash
#!/usr/bin/env bash
source common.sh

# Test: Basic zone access
create_test_world "$TEST_ROOT/world"

nix eval --raw \
    --tectonix-git-dir "$TEST_ROOT/world/.git" \
    --tectonix-git-sha "$(git -C "$TEST_ROOT/world" rev-parse HEAD)" \
    --expr 'builtins.__unsafeTectonixInternalZoneSrc "//areas/tools/dev"'

# Verify output is a store path
```

**`tests/functional/tectonix/lazy-trees.sh`:**
```bash
#!/usr/bin/env bash
source common.sh

# Test: Lazy trees don't copy until needed
create_test_world "$TEST_ROOT/world"

# Access zone with lazy-trees
result=$(nix eval --json \
    --option lazy-trees true \
    --tectonix-git-dir "$TEST_ROOT/world/.git" \
    --tectonix-git-sha "$(git -C "$TEST_ROOT/world" rev-parse HEAD)" \
    --expr '(builtins.__unsafeTectonixInternalZone "//areas/tools/dev").treeSha')

# Verify the store path doesn't exist yet (virtual)
# Then trigger derivation build and verify it exists
```

**`tests/functional/tectonix/dirty-zones.sh`:**
```bash
#!/usr/bin/env bash
source common.sh

# Test: Dirty zone detection
create_test_world "$TEST_ROOT/world"

# Modify a file
echo "modified" >> "$TEST_ROOT/world/areas/tools/dev/zone.nix"

result=$(nix eval --json \
    --tectonix-git-dir "$TEST_ROOT/world/.git" \
    --tectonix-git-sha "$(git -C "$TEST_ROOT/world" rev-parse HEAD)" \
    --tectonix-checkout-path "$TEST_ROOT/world" \
    --expr '(builtins.__unsafeTectonixInternalZone "//areas/tools/dev").dirty')

[[ "$result" == "true" ]] || fail "Expected dirty=true"
```

**`tests/functional/tectonix/deduplication.sh`:**
```bash
#!/usr/bin/env bash
source common.sh

# Test: Same tree SHA across commits returns same path
create_test_world "$TEST_ROOT/world"

sha1=$(git -C "$TEST_ROOT/world" rev-parse HEAD)

# Make commit that doesn't touch //areas/tools/dev
echo "other" >> "$TEST_ROOT/world/README.md"
git -C "$TEST_ROOT/world" add -A && git -C "$TEST_ROOT/world" commit -m "other"
sha2=$(git -C "$TEST_ROOT/world" rev-parse HEAD)

# Get zone paths for both commits
path1=$(nix eval --raw \
    --tectonix-git-dir "$TEST_ROOT/world/.git" \
    --tectonix-git-sha "$sha1" \
    --expr 'builtins.__unsafeTectonixInternalZoneSrc "//areas/tools/dev"')

path2=$(nix eval --raw \
    --tectonix-git-dir "$TEST_ROOT/world/.git" \
    --tectonix-git-sha "$sha2" \
    --expr 'builtins.__unsafeTectonixInternalZoneSrc "//areas/tools/dev"')

# Should be same path due to tree SHA deduplication
[[ "$path1" == "$path2" ]] || fail "Expected same path for unchanged zone"
```

**`tests/functional/tectonix/errors.sh`:**
```bash
#!/usr/bin/env bash
source common.sh

# Test: Error handling
create_test_world "$TEST_ROOT/world"

# Missing git-dir setting
expect_failure nix eval \
    --tectonix-git-sha "abc123" \
    --expr 'builtins.__unsafeTectonixInternalManifest'

# Invalid SHA
expect_failure nix eval \
    --tectonix-git-dir "$TEST_ROOT/world/.git" \
    --tectonix-git-sha "0000000000000000000000000000000000000000" \
    --expr 'builtins.__unsafeTectonixInternalManifest'

# Non-zone path
expect_failure nix eval \
    --tectonix-git-dir "$TEST_ROOT/world/.git" \
    --tectonix-git-sha "$(git -C "$TEST_ROOT/world" rev-parse HEAD)" \
    --expr 'builtins.__unsafeTectonixInternalZoneSrc "//areas/tools/dev/subdir"'
```

---

## Phase 7: Error Handling Edge Cases

**Location:** `src/libexpr-tests/tectonix.cc`

### 7.1 Settings Validation

| Test Case | Expected Error |
|-----------|----------------|
| Empty `tectonix-git-dir` | "must be specified" |
| Non-existent git dir | Git open error |
| Empty `tectonix-git-sha` | "must be specified" |
| Malformed SHA | Parse error |
| SHA not in repo | "not found in repository" |

### 7.2 Manifest Errors

| Test Case | Expected Error |
|-----------|----------------|
| Missing manifest file | "does not exist" |
| Invalid JSON | JSON parse error |
| Missing "id" field | "missing or non-string 'id' field" |
| Non-string "id" value | "non-string 'id' field" |

### 7.3 Zone Path Errors

| Test Case | Expected Error |
|-----------|----------------|
| Path not in manifest | "not a zone root" |
| Subpath of zone | "not a zone root" |
| Path traversal (`..`) | "invalid path component" |

### 7.4 Git Operations Errors

| Test Case | Expected Error |
|-----------|----------------|
| Tree SHA not found | "not found in world repository" |
| Non-tree SHA for tree access | Type error |
| Non-commit SHA for commit access | "not a valid commit" |

---

## Implementation Priority

1. **High Priority** (blocking issues identified in review):
   - Phase 2: Git-utils `odbOnly` tests
   - Phase 3.7: Dirty zone detection tests
   - Phase 4.5-4.6: Zone builtin tests with validation

2. **Medium Priority** (core functionality):
   - Phase 3: All EvalState method tests
   - Phase 4: All builtin tests
   - Phase 6: Basic functional tests

3. **Lower Priority** (polish):
   - Phase 1: Helper function unit tests
   - Phase 5: Thread-safety tests
   - Phase 7: Comprehensive error handling tests

---

## Test Data Requirements

### Minimal Test Repository Structure

```
test-world/
├── .git/
├── .meta/
│   └── manifest.json
├── areas/
│   ├── tools/
│   │   ├── dev/
│   │   │   ├── zone.nix
│   │   │   └── src/
│   │   │       └── main.cc
│   │   └── tec/
│   │       └── zone.nix
│   └── platform/
│       └── core/
│           └── zone.nix
└── README.md
```

### Manifest Content

```json
{
  "//areas/tools/dev": { "id": "W-000001" },
  "//areas/tools/tec": { "id": "W-000002" },
  "//areas/platform/core": { "id": "W-000003" }
}
```

---

## Running Tests

### Unit Tests

```bash
# Build and run all unit tests
meson test -C build libexpr-tests libfetchers-tests

# Run specific test file
meson test -C build libexpr-tests --test-args='--gtest_filter=Tectonix*'
```

### Functional Tests

```bash
# Run tectonix functional tests
make -C tests/functional tectonix

# Run specific test
./tests/functional/tectonix/basic.sh
```

---

## Coverage Goals

| Component | Line Coverage Target |
|-----------|---------------------|
| `src/libexpr/eval.cc` (tectonix methods) | 90% |
| `src/libexpr/primops/tectonix.cc` | 95% |
| `src/libfetchers/git-utils.cc` (new methods) | 90% |

---

## Notes

- Thread-safety tests should be run with `--eval-cores=4` to stress concurrent access
- Functional tests need a real git repository, not just objects
- Consider property-based testing for path normalization edge cases
- The `odbOnly` mode is critical for reftable-enabled repositories (common in enterprise)
