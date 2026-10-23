---
synopsis: "`allowed-uris` now also applies to `builtins.readFile`/`readDir`/`readFileType`"
prs: []
issues: [2596]
---

Under `restrict-eval`, the `allowed-uris` setting previously only granted access to fetchers
like `builtins.fetchurl` and `fetchGit`. `builtins.readFile`, `builtins.readDir`, and
`builtins.readFileType` ignored it and only consulted the paths allow-listed via `-I`/
`NIX_PATH`. These builtins now also respect `allowed-uris`, consistent with the fetchers.
