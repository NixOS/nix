---
synopsis: "GitLab flakerefs now allow for nested organisations"
prs: 9163
---

GitLab allows to nest organisations (`gitlab.com/org/suborg/subsuborg/repo`).
The flakeref URL for GitLab repositories now supports that.

This means that it isn't possible any more to use the `/<ref>` (or `/<rev>`) syntax for GitLab repositories (as that would be ambiguous), and users need to pass an explicit query parameter (`?ref=<ref>` or `?rev=<rev>`).

```console
# Correctly points to https://gitlab.com/gitlab-org/build/CNG
$ nix flake prefetch gitlab:gitlab-org/build/CNG
Downloaded 'gitlab:gitlab-org/build/CNG/66c66a2b878c07ebe50ebcd53a14252b28851481' to '/nix/store/41m1gpah15xh75d6a0758pchiv5r323r-source' (hash 'sha256-RZn9FxNZb1w9WGzOBEr5byDbbOmC2HOWWsJSM0fi1PA=').
# Fails because https://gitlab.com/gitlab-org/cli/main isn't a repository
$ nix flake prefetch gitlab:gitlab-org/cli/main
error:
    [...]
# Correctly fetches the `main` branch of https://gitlab.com/gitlab-org/cli
$ nix flake prefetch gitlab:gitlab-org/cli\?ref=main
Downloaded 'gitlab:gitlab-org/cli/2de2d082c8e11be778669b699847c573d40f9754?narHash=sha256-gXONllQBQRbfWHmKGAJOeoej8fQ%2Bq8Rnj2734%2BDsrhw%3D' to '/nix/store/4p7nnj48xnhmcyajh5xvgr461gk98pnf-source' (hash 'sha256-gXONllQBQRbfWHmKGAJOeoej8fQ+q8Rnj2734+Dsrhw=').
```
