---
synopsis: Support SCP-like URLs in fetchGit and type = "git" flake inputs
prs: [14863]
issues: [14852, 14867]
---

Nix now (once again) recognizes [SCP-like syntax for Git URLs](https://git-scm.com/docs/git-clone#_git_urls). This partially
restores compatibility with Nix 2.3 for `fetchGit`. The following syntax is once again supported:

```nix
builtins.fetchGit "host:/absolute/path/to/repo"
```

Nix also passes through the tilde (for home directories) verbatim:

```nix
builtins.fetchGit "host:~/relative/to/home"
```

IPv6 addresses also supported when bracketed:

```nix
builtins.fetchGit "user@[::1]:~/relative/to/home"
```

`builtins.fetchTree` also supports this syntax now:

```nix
builtins.fetchTree { type = "git"; url = "host:/path/to/repo"; }
```
