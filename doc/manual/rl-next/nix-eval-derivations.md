---
synopsis: "`nix eval` prints derivations as `.drv` paths"
prs: 10200
---

`nix eval` will now print derivations as their `.drv` paths, rather than as
attribute sets. This makes commands like `nix eval nixpkgs#bash` terminate
instead of infinitely looping into recursive self-referential attributes:

```ShellSession
$ nix eval nixpkgs#bash
«derivation /nix/store/m32cbgbd598f4w299g0hwyv7gbw6rqcg-bash-5.2p26.drv»
```
