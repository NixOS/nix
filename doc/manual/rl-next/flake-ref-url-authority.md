---
synopsis: Support URL authority in git forge flake refs
issues: [6304, 7970, 11135, 14064]
prs: [11467]
---

Git forge inputs now accept the URL authority form proposed in #6304, so
non-default hosts can be written directly in the input URL. For example,
`gitlab://gitlab.example.org/org/group/repo` fetches a GitLab project
from `gitlab.example.org`, and `gitea://git.example.org/owner/repo`
uses the Gitea-compatible archive fetcher against `git.example.org`.

Built-in Git forge input schemes are provided for `github`, `gitlab`,
`sourcehut`, `codeberg`, `gitea`, `forgejo`, `cgit`, and `bitbucket`. Branch
and tag names can be written with `?ref=...`, while commit hashes can be written
with `?rev=...`. The older path-ref form remains for backwards compatibility
when the URL has no authority and no `ref`/`rev` query parameter is present.
The `codeberg` scheme always targets `codeberg.org`; use `gitea://` or
`forgejo://` for custom Gitea-compatible hosts.

GitLab namespaces and cgit repository paths may span multiple path segments
in query/authority form, so nested GitLab groups no longer need `%2F` in the
owner path.
