# Nix Local Store SQLite Schema History

A complete history of schema changes to the Nix local store SQLite database,
with particular attention to which changes break the ability to roll back to
older Nix versions.

## Table of Contents

- [Schema Versioning Mechanisms](#schema-versioning-mechanisms)
- [Summary of Rollback-Breaking Changes](#summary-of-rollback-breaking-changes)
- [Detailed Change History](#detailed-change-history)
  - [Schema 4: Delete substitutes table (2007)](#schema-4-delete-substitutes-table-2007)
  - [Schema 5: BerkeleyDB to SQLite conversion (2010)](#schema-5-berkeleydb-to-sqlite-conversion-2010)
  - [Schema 6: Integer IDs for ValidPaths/Refs (2010)](#schema-6-integer-ids-for-validpathsrefs-2010)
  - [Add deriver column to ValidPaths (2010)](#add-deriver-column-to-validpaths-2010)
  - [Add DerivationOutputs table (2010)](#add-derivationoutputs-table-2010)
  - [Add narSize column to ValidPaths (2010)](#add-narsize-column-to-validpaths-2010)
  - [Schema 7: Clear immutable bits (2013)](#schema-7-clear-immutable-bits-2013)
  - [Schema 8: Add ultimate and sigs columns (2016)](#schema-8-add-ultimate-and-sigs-columns-2016)
  - [Schema 9: Drop FailedPaths table (2016)](#schema-9-drop-failedpaths-table-2016)
  - [Schema 10: Add ca column (2016)](#schema-10-add-ca-column-2016)
  - [CA derivation schema: Realisations table (2020)](#ca-derivation-schema-realisations-table-2020)
  - [CA schema v2-v4: Table recreation and indexes (2021-2023)](#ca-schema-v2-v4-table-recreation-and-indexes-2021-2023)
  - [Schema version file durability improvement (2022)](#schema-version-file-durability-improvement-2022)
  - [SchemaMigrations table: Fine-grained migrations (2024)](#schemamigrations-table-fine-grained-migrations-2024)
  - [Cleanup: Remove ancient CA schema migrations (2024)](#cleanup-remove-ancient-ca-schema-migrations-2024)
  - [Remove dependent realisations (2025)](#remove-dependent-realisations-2025)
  - [Drop redundant IndexReferrer index (2026, feature branch)](#drop-redundant-indexreferrer-index-2026-feature-branch)
  - [Add pathName/deriverName columns (2026, this PR)](#add-pathnamederivername-columns-2026-this-pr)
- [NAR Info Disk Cache Version History](#nar-info-disk-cache-version-history)

## Schema Versioning Mechanisms

The Nix store uses two parallel schema versioning mechanisms:

### 1. Global schema version file (`/nix/var/nix/db/schema`)

An integer stored in a flat file on disk. Currently `nixSchemaVersion = 10`
(defined in `src/libstore/include/nix/store/local-store.hh`). When an older
Nix version finds a schema version higher than what it supports, it refuses to
open the store with:

> "current Nix store schema is version %1%, but I only support %2%"

This is the "hard stop" mechanism that prevents silent data corruption.

### 2. SchemaMigrations table (introduced October 2024)

A table inside the SQLite database itself, tracking individual named
migrations. This was explicitly designed so that backward-compatible schema
changes (adding tables, nullable columns, indexes) **do not require bumping the
global schema version**, allowing older Nix versions to continue accessing the
database.

### 3. NAR info disk cache filename versioning

The NAR info cache uses a separate SQLite database with the version encoded in
the filename (e.g., `binary-cache-v8.sqlite`). Version bumps create a new file;
old Nix continues using its own version file. See
[NAR Info Disk Cache Version History](#nar-info-disk-cache-version-history).

## Summary of Rollback-Breaking Changes

| Schema | Date | Change | Commit | Breaks Rollback? |
|---|---|---|---|---|
| 4 | 2007-08 | Delete substitutes table | [`3757ee5`][c-3757ee5] | **YES** — version bump |
| 5 | 2010-02 | BerkeleyDB to SQLite | [`c1a07f9`][c-c1a07f9] | **YES** — total format change |
| 6 | 2010-02 | Integer IDs in ValidPaths/Refs | [`dbddac0`][c-dbddac0] | **YES** — version bump |
| — | 2010-02 | Add `deriver` column | [`a053d2d`][c-a053d2d] | No (nullable column) |
| — | 2010-02 | Add DerivationOutputs table | [`299ff64`][c-299ff64] | No (new table) |
| — | 2010-11 | Add `narSize` column | [`a3883cb`][c-a3883cb] | No (nullable column) |
| 7 | 2013-01 | Clear immutable bits | [`def5160`][c-def5160] | **YES** — version bump |
| 8 | 2016-03 | Add `ultimate` + `sigs` columns | [`9cee600`][c-9cee600] | **YES** — version bump (\*) |
| 9 | 2016-04 | DROP TABLE FailedPaths | [`8cffec8`][c-8cffec8] | **YES** — version bump + destructive DDL |
| 10 | 2016-08 | Add `ca` column | [`d961c29`][c-d961c29] | **YES** — version bump (\*) |
| CA v1 | 2020-10 | Add Realisations table | [`3ac9d74`][c-3ac9d74] | No (experimental, new table) |
| CA v2-v4 | 2021-23 | Recreate Realisations, add indexes | [`edfc5b2`][c-edfc5b2] | **YES** (CA only) — CA version bump |
| — | 2022-09 | Schema file durability (fsync) | [`1b59502`][c-1b59502] | No (operational) |
| — | 2024-10 | SchemaMigrations table | [`27ea437`][c-27ea437] | No (old Nix ignores unknown tables) |
| — | 2024-10 | Remove ancient CA migrations | [`94f649f`][c-94f649f] | No (code cleanup) |
| — | 2025-10 | Remove dependent realisations | [`4a5d960`][c-4a5d960] | No (tables left in place) |
| — | 2026-03 | Drop IndexReferrer (feature branch) | [`5286c04`][c-5286c04] | No (index drop) |
| — | **2026-03** | **Add pathName/deriverName columns** | **(this PR)** | **No** (new nullable columns + indexes) |

(\*) The SchemaMigrations commit [`27ea437`][c-27ea437] explicitly noted that
schema versions 8 and 10 "could have been handled by this mechanism in a
backward-compatible way" — the version bumps were unnecessarily breaking.

[c-3757ee5]: https://github.com/NixOS/nix/commit/3757ee589f46a401fdacaa2126e6bf4902eee23d
[c-c1a07f9]: https://github.com/NixOS/nix/commit/c1a07f94451cfa93aa9ac986188d0e9a536e4b9f
[c-dbddac0]: https://github.com/NixOS/nix/commit/dbddac0fe91072b402ccb3801c952e3159f0cba4
[c-a053d2d]: https://github.com/NixOS/nix/commit/a053d2d8e53f2967c64ab2b204727e4c27f06c0e
[c-299ff64]: https://github.com/NixOS/nix/commit/299ff64812ce166d230f1b630f794be226c7a178
[c-a3883cb]: https://github.com/NixOS/nix/commit/a3883cbd28057a3dd2573f77dcda9a26faaac555
[c-def5160]: https://github.com/NixOS/nix/commit/def5160b614a59a0aa96fe2252e3daa00146e061
[c-9cee600]: https://github.com/NixOS/nix/commit/9cee600c88d2a23b872be1c175450011a6d52152
[c-8cffec8]: https://github.com/NixOS/nix/commit/8cffec84859cec8b610a2a22ab0c4d462a9351ff
[c-d961c29]: https://github.com/NixOS/nix/commit/d961c29c9c5e806ff7c46c855a1e9d2b6cae593b
[c-3ac9d74]: https://github.com/NixOS/nix/commit/3ac9d74eb1de0f696bb0384132f7ecc7d057f9d6
[c-edfc5b2]: https://github.com/NixOS/nix/commit/edfc5b2f127bfbaebbd48fcd7b35034345ce2cfa
[c-1b59502]: https://github.com/NixOS/nix/commit/1b595026e18afb050de3f62ded8f7180bc8b2b0e
[c-27ea437]: https://github.com/NixOS/nix/commit/27ea43781371cad717077ae723b11a79c0d0fc78
[c-94f649f]: https://github.com/NixOS/nix/commit/94f649fad55432e75e5f22815738b90f9cd81c57
[c-4a5d960]: https://github.com/NixOS/nix/commit/4a5d960952ac1d87690e5282ed60d09f1b2a5841
[c-5286c04]: https://github.com/NixOS/nix/commit/5286c0477d77fb919df890324d3a510a199f450e

## Detailed Change History

### Schema 4: Delete substitutes table (2007)

**Commit:** [`3757ee5`][c-3757ee5] — 2007-08-13

Bumped schema version from 3 to 4. Deleted the BerkeleyDB `substitutes` table,
which was no longer used. Added `upgradeStore11()` migration function.

**Rollback impact:** BREAKING. Old Nix (0.10 era) refuses to open since schema > 3.

---

### Schema 5: BerkeleyDB to SQLite conversion (2010)

**Commit:** [`c1a07f9`][c-c1a07f9] — 2010-02-18

Converted the entire Nix database from BerkeleyDB to SQLite. Created the
initial `ValidPaths`, `Refs`, and `FailedPaths` tables with text-based path
references.

**Rollback impact:** TOTAL BREAKAGE. Completely new storage format.

---

### Schema 6: Integer IDs for ValidPaths/Refs (2010)

**Commit:** [`dbddac0`][c-dbddac0] — 2010-02-18

Changed `ValidPaths` primary key from `path text` to `id integer primary key
autoincrement`. Changed `Refs` table from text-based `(referrer, reference)` to
integer-based foreign keys. Dramatically reduced DB size (93 MiB to 18 MiB on
the developer's laptop).

**Rollback impact:** BREAKING. Old schema 5 code expected text-based Refs table.

---

### Add deriver column to ValidPaths (2010)

**Commit:** [`a053d2d`][c-a053d2d] — 2010-02-18

`ALTER TABLE ValidPaths ADD COLUMN deriver text`. Done as part of the schema 6
era.

**Rollback impact:** Safe. SQLite ignores unknown columns in queries that don't
reference them. Old code would not populate the deriver, but would not crash.

---

### Add DerivationOutputs table (2010)

**Commit:** [`299ff64`][c-299ff64] — 2010-02-22

Added new `DerivationOutputs` table and `IndexDerivationOutputs` index for
garbage collector efficiency.

**Rollback impact:** Safe. New table ignored by code that does not reference it.

---

### Add narSize column to ValidPaths (2010)

**Commit:** [`a3883cb`][c-a3883cb] — 2010-11-16

`ALTER TABLE ValidPaths ADD COLUMN narSize integer`. Initially used
`sqlite3_table_column_metadata()` to check if column already existed.

**Rollback impact:** Safe. Nullable column, old code ignores it.

---

### Schema 7: Clear immutable bits (2013)

**Commit:** [`def5160`][c-def5160] — 2013-01-03

Schema version bumped from 6 to 7. Cleared immutable file attributes in the
store.

**Rollback impact:** BREAKING. Old Nix (< 1.3) refuses to open since schema > 6.

---

### Schema 8: Add ultimate and sigs columns (2016)

**Commit:** [`9cee600`][c-9cee600] — 2016-03-30

`ALTER TABLE ValidPaths ADD COLUMN ultimate integer` and
`ALTER TABLE ValidPaths ADD COLUMN sigs text`. Schema version bumped from 7 to
8.

**Rollback impact:** BREAKING due to the global schema version bump. However,
the SchemaMigrations commit ([`27ea437`][c-27ea437]) later noted: *"Schema
versions 8 and 10 could have been handled by this mechanism in a
backward-compatible way as well"* — meaning this version bump was unnecessary
since both changes were nullable `ALTER TABLE ADD COLUMN`.

---

### Schema 9: Drop FailedPaths table (2016)

**Commit:** [`8cffec8`][c-8cffec8] — 2016-04-08

`DROP TABLE FailedPaths`. Schema version bumped from 8 to 9. Removed the failed
build caching feature.

**Rollback impact:** BREAKING. The `DROP TABLE` is destructive — old Nix code
that queries `FailedPaths` would get errors. The version bump also causes a hard
rejection.

---

### Schema 10: Add ca column (2016)

**Commit:** [`d961c29`][c-d961c29] — 2016-08-03

`ALTER TABLE ValidPaths ADD COLUMN ca text`. For marking content-addressed paths.
Schema version bumped from 9 to 10.

**Rollback impact:** BREAKING due to version bump, but as noted above, this was
another nullable `ADD COLUMN` that could have been backward-compatible.

---

### CA derivation schema: Realisations table (2020)

**Commit:** [`3ac9d74`][c-3ac9d74] — 2020-10-20

Introduced `ca-specific-schema.sql` with `Realisations` table, tracked by a
separate `/nix/var/nix/db/ca-schema` version file. Only created when the
`ca-derivations` experimental feature is enabled.

**Rollback impact:** Mostly safe. Tables only created when feature is on, and old
Nix ignores unknown tables.

---

### CA schema v2-v4: Table recreation and indexes (2021-2023)

**Commits:** [`edfc5b2`][c-edfc5b2] and others — 2021-2023

Migrated `Realisations` table to add autoincrement `id` primary key (recreated
as `Realisations2`, then renamed). Added `RealisationsRefs` table for
inter-realisation dependencies. Added indexes. CA schema version bumped through
versions 2, 3, and 4.

**Rollback impact:** BREAKING for CA schema. Each bump prevented rollback to
older CA-aware Nix versions. Old code expecting the original primary key
structure would fail.

---

### Schema version file durability improvement (2022)

**Commit:** [`1b59502`][c-1b59502] — 2022-09-19

Added `fsync` after writing the schema version file and its parent directory to
prevent corruption during crashes (issue #7064).

**Rollback impact:** None. Operational improvement only.

---

### SchemaMigrations table: Fine-grained migrations (2024)

**Commit:** [`27ea437`][c-27ea437] — 2024-10-10

Major architectural change. Introduced `SchemaMigrations` table for tracking
individual named migrations. Replaced the ad-hoc `ca-schema` version file. The
commit message is explicit about backward compatibility:

> *"Backward-compatible schema changes (e.g. those that add tables or nullable
> columns) now no longer need a change to the global schema file
> (/nix/var/nix/db/schema). Thus, old Nix versions can continue to access the
> database."*

**Rollback impact:** Safe. The `SchemaMigrations` table is ignored by old Nix.

---

### Cleanup: Remove ancient CA schema migrations (2024)

**Commit:** [`94f649f`][c-94f649f] — 2024-10-03

Removed support for migrating from CA schema versions 1-3 (more than 3 years
old). Now only supports schema version 0 (fresh) or version 4.

**Rollback impact:** None. Code cleanup only.

---

### Remove dependent realisations (2025)

**Commit:** [`4a5d960`][c-4a5d960] — 2025-10-13

Removed the `dependentRealisations` field from `Realisation` and stopped using
the `RealisationsRefs` table. The commit message explicitly notes:

> *"The SQL tables are left in place because there is no point inducing a
> migration now, when we will be immediately landing more changes after this
> that also require schema changes. They will simply be ignored."*

**Rollback impact:** Safe. Tables left in place, just not queried.

---

### Drop redundant IndexReferrer index (2026, feature branch)

**Commit:** [`5286c04`][c-5286c04] — 2026-03-09 (branch `drop-redundant-indexreferrer`)

`DROP INDEX IF EXISTS IndexReferrer` on `Refs(referrer)`. Tracked via the
`SchemaMigrations` system.

**Rollback impact:** Safe. Dropping an index does not affect correctness. Old
Nix does not recreate indexes on existing stores.

---

### Add pathName/deriverName columns (2026, this PR)

**Date:** 2026-03-12

Adds new `pathName` and `deriverName` columns to `ValidPaths`, and a new
`pathName` column to `DerivationOutputs`. These store the efficient baseName
format (e.g. `hash-name`) while the legacy `path` and `deriver` columns
continue to store the full format (e.g. `/nix/store/hash-name`).

New Nix reads from the new columns (fast — uses `StorePath()` constructor
directly instead of `parseStorePath()`). Old Nix reads from the legacy columns
(still works — full paths are always written). A startup backfill function
populates the new columns for any rows inserted by older Nix after a rollback.

This is a **backward-compatible** change:
- Only adds nullable columns and indexes (no version bump needed)
- Old Nix ignores the new columns and continues using `path`/`deriver`
- The `SchemaMigrations` mechanism tracks the column additions
- No data in existing columns is modified

**Rollback impact:** Safe. Old Nix ignores the new columns. The legacy columns
always contain valid full paths.

## NAR Info Disk Cache Version History

The NAR info disk cache uses a separate SQLite database with the version in the
filename. Each version bump creates a new empty database file — old Nix
continues using the old file, so these changes are always safe for rollback.

| Version | Change |
|---|---|
| v3 | Initial creation (2016) |
| v4 | Negative cache lookup support |
| v5 | Added `ca` column to NARs table (2018) |
| v6 | WAL mode change (2025) |
| v7 | Revert hash modulo experiment (2025) |
| v8 | Current — BuildTrace table with structured columns |
