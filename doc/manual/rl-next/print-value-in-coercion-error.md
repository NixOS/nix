synopsis: Coercion errors include the failing value
issues: #561
prs: #9553
description: {

The `error: cannot coerce a <TYPE> to a string` message now includes the value which caused the error.

Previously, a failed string coercion produced a confusing error message if the trace didn't show where the offending value was defined:

```bash
$ nix-instantiate --eval --expr '
let x = { a = 1; }; in

"${x}"
'
error:
       … while evaluating a path segment

         at «string»:4:2:

            3|
            4| "${x}"
             |  ^
            5|

       error: cannot coerce a set to a string
```

Now, the error message includes the value itself:

```bash
$ nix-instantiate --eval --expr '
let x = { a = 1; }; in

"${x}"
'
error:
       … while evaluating a path segment

         at «string»:4:2:

            3|
            4| "${x}"
             |  ^
            5|

       error: cannot coerce a set to a string: { a = 1; }
```

}
