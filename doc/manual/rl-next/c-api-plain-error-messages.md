---
synopsis: "C API error messages are plain text, and trace items can be read one by one"
issues: [14017]
prs: [16712]
---

`nix_err_msg` and `nix_err_info_msg` no longer include ANSI color codes in the returned message.

The new `nix_err_info_trace_count` and `nix_err_info_trace_msg` return the trace items of a Nix error separately, as plain text, whether or not `show-trace` is set.
