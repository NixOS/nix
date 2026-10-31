---
synopsis: "C API: `nix_eval_state_get_stat_int` and `nix_eval_state_get_stats_json`"
prs: [16675]
---

The C API can now read evaluation statistics from `EvalState`.
`nix_eval_state_get_stats_json()` can return all of them in `NIX_SHOW_STATS=1` JSON format.
`nix_eval_state_get_stat_int()` can return a single counter.
Counting is off by default. `nix_eval_stats_enable()` or `NIX_SHOW_STATS` can enable counting.
