---
synopsis: New `parallel-store-jobs` setting to oversubscribe store-path worker pools
prs: []
---

Two `ThreadPool` instances that drive batch operations over store paths were
previously hardcoded to `std::thread::hardware_concurrency()`:

- `Store::queryValidPaths` — the bulk path-validity check that runs at the
  start of `nix copy` (one HEAD request per path against the destination).
- `Store::addMultipleToStore`'s `processGraph` workers — the upload/copy
  workers that actually push NARs to a remote store.

For I/O-bound workloads — most notably, uploading large closures to a remote
binary cache over a high-latency link — these workers spend the vast majority
of their time waiting on network round-trips, so the core count is the wrong
ceiling. Capping concurrency at `hardware_concurrency()` leaves substantial
network bandwidth on the table even when `http-connections` is set much
higher.

The new `parallel-store-jobs` setting gives operators a single knob to control
both pools. Default is `0`, which preserves the existing
`hardware_concurrency()` behavior — no change for anyone who doesn't set it.

```
nix copy --to s3://my-cache?... --option parallel-store-jobs 64
```

is a possible setting for evaluator instances pushing closures to S3/GCS,
where each thread is mostly waiting on RTT.
