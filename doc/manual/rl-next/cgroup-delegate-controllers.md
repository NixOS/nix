---
synopsis: "Daemon delegates cgroup controllers so build cgroups expose memory and I/O stats"
issues: []
---

When the daemon runs with `use-cgroups` enabled, it now enables the `cpu`,
`memory`, `io` and `pids` controllers in its service cgroup's
`cgroup.subtree_control` after moving itself into a leaf sub-cgroup.

Previously, per-build cgroups inherited no controllers (the cgroup-v2 "no
internal process" rule prevents enabling them while the cgroup has member
processes), so only the always-present `cpu.stat` was readable; `memory.peak`,
`io.stat` and `memory.events` were absent. With delegation in place, consumers
can read peak memory, disk I/O and OOM events per build.
