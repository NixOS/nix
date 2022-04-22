# Advanced Topic: Related Work

## Bazel

TODO skylark and layering.

TODO being monadic, if RFC 92.

## Build Systems à la Carte

TODO user-choosen keys vs keys chosen automatically?
Purity in face of dynamic tasks (no conflicts, guaranteed).

TODO Does Nix constitute a different way to be be monadic?
Purity of keys, as mentioned.
Dynamic tasks/keys vs dynamic dependencies.
(Not sure yet.)

## Lazy evaluation

We clearly have thunks that produce thunks, but less clearly functions that produce functions.

Do we have open terms?

Do we hve thunks vs expressions distinction?
c.f. John Shutt's modern fexprs, when the syntax can "leak".

## Machine models

TODO
Derivations as store objects via drv files makes Nix a "Von Neumann" archicture.
Can also imagine a "Harvard" archicture where derivations are stored separately?
Can we in general imagine N heaps for N different sorts of objects?

TODO
Also, leaning on the notion of "builtin builders" more, having multiple different sorts of user-defined builders too.
The builder is a black box as far as the Nix model is concerned.
