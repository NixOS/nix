synopsis: Type errors include the failing value
issues: #561
prs: #9554
description: {

In errors like `value is an integer while a list was expected`, the message now
includes the failing value.

Before:

```
error:
       … while calling the 'splitVersion' builtin

         at bad.nix:5:3:

            4| in
            5|   builtins.splitVersion pkgs.haskell.compiler
             |   ^
            6|

       … while evaluating the first argument passed to builtins.splitVersion

         at «none»:0: (source not available)

       error: value is a set while a string was expected
```

After:

```
error:
       … while calling the 'splitVersion' builtin

         at bad.nix:5:3:

            4| in
            5|   builtins.splitVersion pkgs.haskell.compiler
             |   ^
            6|

       … while evaluating the first argument passed to builtins.splitVersion

       error: value is a set while a string was expected: { ghc810 = <CODE>;
       ghc8102Binary = <CODE>; ghc8102BinaryMinimal = <CODE>; ghc8107 = <CODE>;
       ghc8107Binary = <CODE>; ghc8107BinaryMinimal = <CODE>; ghc865Binary =
       <CODE>; ghc88 = <CODE>; ghc884 = <CODE>; ghc90 = <CODE>; ghc902 =
       <CODE>; ghc92 = <CODE>; ghc924 = <CODE>; ghc924Binary = <CODE>;
       ghc924BinaryMinimal = <CODE>; ghc94 = <CODE>; ghc942 = <CODE>; ghcHEAD =
       <CODE>; ghcjs = <CODE>; ghcjs810 = <CODE>; integer-simple = <CODE>;
       native-bignum = <CODE>; }
```

}
