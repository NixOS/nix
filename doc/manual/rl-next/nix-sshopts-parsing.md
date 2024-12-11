---
synopsis: "Improved `NIX_SSHOPTS` parsing for better SSH option handling"
issues: [5181]
prs: [12020]
---

The parsing of the `NIX_SSHOPTS` environment variable has been improved to handle spaces and quotes correctly.
Previously, incorrectly split SSH options could cause failures in CLIs like `nix-copy-closure`,
especially when using complex ssh invocations such as `-o ProxyCommand="ssh -W %h:%p ..."`.

This change introduces a `shellSplitString` function to ensure
that `NIX_SSHOPTS` is parsed in a manner consistent with shell
behavior, addressing common parsing errors.

For example, the following now works as expected:

```bash
export NIX_SSHOPTS='-o ProxyCommand="ssh -W %h:%p ..."'
```

This update improves the reliability of SSH-related operations using `NIX_SSHOPTS` across Nix CLIs.
