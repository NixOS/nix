---
synopsis: "Path-like flake references detect Mercurial checkouts"
---

A path-like flake reference such as `.` used to become a `git+file:` input only inside a Git repository, and a `path:` input everywhere else.
In a Mercurial checkout that copied the whole directory to the store, `.hg` and untracked files included, and the flake had no `rev`.

Such a reference now resolves to `hg+file:`, so only tracked files are copied.
This needs the `hg` executable, as `hg+file:` inputs always did.

Input schemes, including ones from [`plugin-files`](@docroot@/command-ref/conf-file.md#conf-plugin-files), can resolve a path-like flake reference to their own scheme by implementing `InputScheme::localRepoURL()`.
