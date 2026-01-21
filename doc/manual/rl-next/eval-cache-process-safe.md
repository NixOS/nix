---
synopsis: "Eval cache is now process-safe for concurrent access"
prs: []
---

The evaluation cache now uses per-operation transactions with retry logic instead of long-lived transactions, enabling safe concurrent access from multiple Nix processes. This fixes `SQLITE_BUSY` errors and database corruption that could occur when multiple `nix` commands accessed the same flake's eval cache simultaneously.

Key improvements:
- Write operations use `IMMEDIATE` transactions to avoid writer starvation
- Read operations use `DEFERRED` transactions for consistent multi-query reads
- Automatic retry with exponential backoff when the database is busy
- Graceful degradation: cache errors no longer break evaluation
