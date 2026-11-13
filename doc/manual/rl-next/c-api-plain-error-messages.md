---
synopsis: "C API error messages are plain text"
issues: [14017]
prs: [16712]
---

`nix_err_msg` and `nix_err_info_msg` no longer include ANSI color codes in the returned message.
