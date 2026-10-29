---
synopsis: "Build results report peak memory, block I/O and out-of-memory kills"
prs: [16680]
---

With `use-cgroups`, a build result now carries `memoryPeak`, `ioReadBytes`, `ioWriteBytes` and `oomKills` next to
the CPU times, read from the `memory.peak`, `io.stat` and `memory.events` files of the build cgroup.
The fields appear in the JSON build result and, when both sides negotiate the `build-resource-usage` feature,
in the worker protocol.
