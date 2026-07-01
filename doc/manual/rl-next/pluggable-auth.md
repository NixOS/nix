---
synopsis: Pluggable HTTP authentication
prs: [16087]
issues: [9857]
---

HTTP credentials are now resolved from a configurable list of sources,
set via [`auth-sources`](@docroot@/command-ref/conf-file.md#conf-auth-sources):
`builtin:nix` reads files in `~/.local/share/nix/auth`, and any other
entry is an external helper using the [Git credential
protocol](https://git-scm.com/docs/gitcredentials#_custom_helpers).

This applies to all HTTP transfers, including binary cache substitution
and flake inputs fetched over HTTP. For example, to let Nix authenticate
to GitHub using a token from the `gh` CLI, add a credential helper to
`auth-sources`:

```
auth-sources = builtin:nix git-credential-gh
```

where `git-credential-gh` is a program on `$PATH`:

```bash
#!/usr/bin/env bash
[[ $1 == get ]] || exit 0
grep -q '^host=github.com$' || exit 0
echo "username=token"
echo "password=$(gh auth token)"
```

The new `nix auth fill` command resolves a request against these
sources, like `git credential fill`.
