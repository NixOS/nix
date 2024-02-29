---
synopsis: Debugger prints source position information
prs: 9913
---

The `--debugger` now prints source location information, instead of the
pointers of source location information. Before:

```
nix-repl> :bt
0: while evaluating the attribute 'python311.pythonForBuild.pkgs'
0x600001522598
```

After:

```
0: while evaluating the attribute 'python311.pythonForBuild.pkgs'
/nix/store/hg65h51xnp74ikahns9hyf3py5mlbbqq-source/overrides/default.nix:132:27

   131|
   132|       bootstrappingBase = pkgs.${self.python.pythonAttr}.pythonForBuild.pkgs;
      |                           ^
   133|     in
```
