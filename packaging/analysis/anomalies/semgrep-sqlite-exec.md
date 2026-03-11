# Exception: semgrep-sqlite-exec

## Finding
- **Tool**: semgrep
- **Check**: nix.store.sqlite-exec-non-literal
- **File**: src/libstore/sqlite.cc (SQLite::exec)
- **Severity**: warning

## Code
```cpp
void SQLite::exec(const std::string & stmt)
{
    retrySQLite<void>([&]() {
        if (sqlite3_exec(db, stmt.c_str(), 0, 0, 0) != SQLITE_OK)
            SQLiteError::throw_(db, "executing SQLite statement '%s'", stmt);
    });
}
```

## Analysis
The semgrep rule flags `sqlite3_exec` calls with non-literal string arguments as
potential SQL injection vectors. However, `SQLite::exec()` is an internal utility
method. All call sites pass trusted, internally-constructed SQL strings (pragma
settings, schema creation, etc.). No user input reaches this function.

## Proposed Remediations

### Option A: Add nosemgrep comment
Add `// nosemgrep: nix.store.sqlite-exec-non-literal` on the line.

### Option B: Use prepared statements
Replace `sqlite3_exec` with prepared statements for parameterized queries.
However, most callers pass DDL/pragma statements that don't benefit from preparation.

### Option C: Keep exception (no code change)
The function is internal and safe. Suppress via triage exception only.

## Status
- **Reviewed by**: das
- **Date**: 2026-03-10
- **Decision**: Exception (Option C) — pending maintainer input
