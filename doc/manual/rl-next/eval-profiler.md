---
synopsis: Add stack sampling evaluation profiler
prs: [13220]
---

Nix evaluator now supports stack sampling evaluation profiling via `--eval-profiler flamegraph` setting.
It collects collapsed call stack information to output file specified by
`--eval-profile-file` (`nix.profile` by default) in a format directly consumable
by `flamegraph.pl` and compatible tools like [speedscope](https://speedscope.app/).
Sampling frequency can be configured via `--eval-profiler-frequency` (99 Hz by default).

Unlike existing `--trace-function-calls` this profiler includes the name of the function
being called when it's available.
