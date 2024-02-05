---
synopsis: Stack traces are more compact
prs: 9619
---

Stack traces printed with `--show-trace` are more compact.

Before:

```
error:
       … while evaluating the attribute 'body'

         at /Users/wiggles/nix/tests/functional/lang/eval-fail-assert.nix:4:3:

            3|
            4|   body = x "x";
             |   ^
            5| }

       error: assertion '(arg == "y")' failed

       at /Users/wiggles/nix/tests/functional/lang/eval-fail-assert.nix:2:12:

            1| let {
            2|   x = arg: assert arg == "y"; 123;
             |            ^
            3|
```

After:

```
error:
       … while evaluating the attribute 'body'
         at /Users/wiggles/nix/tests/functional/lang/eval-fail-assert.nix:4:3:
            3|
            4|   body = x "x";
             |   ^
            5| }

       error: assertion '(arg == "y")' failed
       at /Users/wiggles/nix/tests/functional/lang/eval-fail-assert.nix:2:12:
            1| let {
            2|   x = arg: assert arg == "y"; 123;
             |            ^
            3|
```

This was actually released in Nix 2.20, but wasn't added to the release notes
so we're announcing it here. The historical release notes have been updated as well.
