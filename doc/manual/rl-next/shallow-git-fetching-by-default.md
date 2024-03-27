---
synopsis: "`fetchTree` now fetches git repositories shallowly by default"
prs: 10028
---

`builtins.fetchTree` now clones git repositories shallowly by default, which reduces network traffic and disk usage significantly in many cases.

Previously, the default behavior was to clone the full history of a specific tag or branch (eg. `ref`) and only afterwards extract the files of one specific revision.

From now on, the `ref` and `allRefs` arguments will be ignored, except if shallow cloning is disabled by setting `shallow = false`.

The defaults for `builtins.fetchGit` remain unchanged. Here, shallow cloning has to be enabled manually by passing `shallow = true`.
