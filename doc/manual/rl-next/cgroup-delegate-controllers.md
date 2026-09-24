---
synopsis: "Daemon enables cgroup controllers for build cgroups"
prs: [16024]
issues: [9675]
---

With `use-cgroups`, the daemon now enables the `cpu`, `memory`, `io` and `pids`
controllers in its cgroup's `cgroup.subtree_control`, so per-build cgroups
expose `memory.peak`, `io.stat` etc. instead of only `cpu.stat`. This requires
the controllers to be delegated to the daemon's cgroup (e.g. systemd
`Delegate=yes`).
