---
synopsis: "Improve error message for untracked files in git-based flakes"
issues:
prs: [12870]
---

If Nix cannot access a file in a flake because it is not tracked by Git, it now provides clear instructions to resolve the issue:

```
error: The path 'foo/bar' in the repository "/path/to/repo" is not tracked by Git.

       To make it accessible to Nix, you need to add it to Git by running:

       git -C "/path/to/repo" add "foo/bar"
```
