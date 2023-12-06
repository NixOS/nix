synopsis: Coercion errors include the failing value
issues: #561
prs: #9553
description: {

The `error: cannot coerce a <TYPE> to a string` message now includes the value
which caused the error. This makes debugging much easier:

```
$ cat bad.nix
let
  pkgs = import <nixpkgs> {};
  system = pkgs.lib.systems.elaborate "x86_64-linux";
in
  import <nixpkgs> {inherit system;}
```

Previously, attempting to evaluate this expression would produce a confusing error message:

```
$ nix-instantiate --eval bad.nix
error:
       … while evaluating a branch condition

         at /nix/store/m8ah0r1ih2shq35vp3hj1k0m1c4hsfga-nixpkgs/nixpkgs/pkgs/stdenv/booter.nix:64:9:

           63|       go = pred: n:
           64|         if n == len
             |         ^
           65|         then rnul pred

       … while calling the 'length' builtin

         at /nix/store/m8ah0r1ih2shq35vp3hj1k0m1c4hsfga-nixpkgs/nixpkgs/pkgs/stdenv/booter.nix:62:13:

           61|     let
           62|       len = builtins.length list;
             |             ^
           63|       go = pred: n:

       (stack trace truncated; use '--show-trace' to show the full trace)

       error: cannot coerce a set to a string
```

Now, the error message includes the set itself. This makes debugging much
simpler, especially when the trace doesn't show the failing expression:

```
$ nix-instantiate --eval bad.nix
error:
       … while evaluating a branch condition

         at /nix/store/m8ah0r1ih2shq35vp3hj1k0m1c4hsfga-nixpkgs/nixpkgs/pkgs/stdenv/booter.nix:64:9:

           63|       go = pred: n:
           64|         if n == len
             |         ^
           65|         then rnul pred

       … while calling the 'length' builtin

         at /nix/store/m8ah0r1ih2shq35vp3hj1k0m1c4hsfga-nixpkgs/nixpkgs/pkgs/stdenv/booter.nix:62:13:

           61|     let
           62|       len = builtins.length list;
             |             ^
           63|       go = pred: n:

       (stack trace truncated; use '--show-trace' to show the full trace)

       error: cannot coerce a set to a string: { system = "x86_64-linux"; ... }
```

}
