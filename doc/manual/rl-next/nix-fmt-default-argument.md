---
synopsis: Removing the default argument passed to the `nix fmt` formatter
issues: []
prs: [11438]
---

The underlying formatter no longer receives the ". " default argument when `nix fmt` is called with no arguments.

This change was necessary as the formatter wasn't able to distinguish between
a user wanting to format the current folder with `nix fmt .` or the generic
`nix fmt`.

The default behaviour is now the responsibility of the formatter itself, and
allows tools such as treefmt to format the whole tree instead of only the
current directory and below.

Author: [**@zimbatm**](https://github.com/zimbatm)
