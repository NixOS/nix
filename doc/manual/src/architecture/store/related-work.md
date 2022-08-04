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

## Comparison with Git file system model

This is close to Git's model, but with one crucial difference:
Git puts the "permission" info within the directory map's values instead of making it part of the file (blob, in it's parlance) object.

```idris
data GitObject
  = Blob ByteString
  | Tree (Map FileName (Persission, FSO))

data Persission
  = Directory -- IFF paired with tree
  -- Iff paired with blob, one of:
  | RegFile
  | ExecutableFile
  | Symlink
```

So long as the root object is a directory, the representations are isomorphic.
There is no "wiggle room" the git way since whenever the permission info wouldn't matter (e.g. the child object being mapped to is a directory), the permission info must be a sentinel value.

However, if the root object is a blob, there is loss of fidelity.
Since the permission info is used to distinguish executable files, non-executable files, and symlinks, but there isn't a "parent" directory of the root to contain that info, these 3 cases cannot be distinguished.

Git's model matches Unix tradition, but Nix's model is more natural.

## Machine models

Derivations as store objects via drv files makes Nix a "Von Neumann" architecture.
Can also imagine a "Harvard" architecture where derivations are stored separately?
Can we in general imagine N heaps for N different sorts of objects?
See note in the section on [abstract derivations and derived paths](./abstract/drv.md).

TODO
Also, leaning on the notion of "builtin builders" more, having multiple different sorts of user-defined builders too.
The builder is a black box as far as the Nix model is concerned.
