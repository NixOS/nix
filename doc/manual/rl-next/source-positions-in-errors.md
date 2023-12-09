synopsis: Source locations are printed more consistently in errors
issues: #561
prs: #9555
description: {

Source location information is now included in error messages more
consistently. Given this code:

```nix
let
  attr = {foo = "bar";};
  key = {};
in
  attr.${key}
```

Previously, Nix would show this unhelpful message when attempting to evaluate
it:

```
error:
       … while evaluating an attribute name

       error: value is a set while a string was expected
```

Now, the error message displays where the problematic value was found:

```
error:
       … while evaluating an attribute name

         at bad.nix:4:11:

            3|   key = {};
            4| in attr.${key}
             |           ^
            5|

       error: value is a set while a string was expected
```

}
