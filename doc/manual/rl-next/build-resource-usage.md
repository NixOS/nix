---
synopsis: "Resource Usage of Builds in `nix build --json` and Failure Messages"
prs: [16680]
---

`nix build --json` can now report a `resourceUsage` object for each built derivation on Linux with [`use-cgroups`](@docroot@/command-ref/conf-file.md#conf-use-cgroups) enabled, alongside `cpuUser` and `cpuSystem`. Fields include peak memory (`memoryPeak`, including page cache) and the peak amount of swap in use (`memorySwapPeak`). Fields also include bytes read from block devices and bytes written to block devices (`ioReadBytes`, `ioWriteBytes`). Fields include the number of processes that the kernel killed because the build ran out of memory (`oomKilledProcessCount`). The fields omit statistics that the kernel doesn't provide.

Error messages now report that the kernel killed some processes of a failed build because memory ran out.

Daemon clients also receive these statistics when both sides support the `build-resource-usage` feature of the worker protocol.
