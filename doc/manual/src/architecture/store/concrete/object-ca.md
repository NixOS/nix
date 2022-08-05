# Content-Addressing Store Objects

Before in the section on [store paths](./paths.md), we talked abstractly about content-adressing store objects to preduce the digest part of their store paths.
Now we can make that precise.

Recall that store paths have the following form:
```bnf
<realized-path> ::= <store-dir>/<digest>-<name>
```
and that `<digest>` = base-32 representation of the first 160 bits of a SHA-256
  hash of `<s>`; the hash part of the store name

`<s>` = the string `<type>:sha256:<h2>:<store>:<name>`;
  note that it includes the location of the store as well as the
  name to make sure that changes to either of those are reflected
  in the hash (e.g. you won't get `/nix/store/<digest>-name1` and
  `/nix/store/<digest>-name2` with equal hash parts).

<type> = one of:
  `text:<r1>:<r2>:...<rN>`
    for plain text files written to the store using
    addTextToStore(); <r1> ... <rN> are the store paths referenced
    by this path, in the form described by `<realized-path>`
  `source:<r1>:<r2>:...:<rN>[:self]`
    for paths copied to the store using addToStore() when recursive
    = true and hashAlgo = "sha256". Just like in the text case, we
    can have the store paths referenced by the path.
    Additionally, we can have an optional `:self` label to denote self
    reference.
  `output:<id>`
    for either the outputs created by derivations, OR paths copied
    to the store using addToStore() with recursive != true or
    hashAlgo != "sha256" (in that case "source" is used; it's
    silly, but it's done that way for compatibility).  `<id>` is the
    name of the output (usually, "out").

`<h2>` = base-16 representation of a SHA-256 hash of:
  if `<type>` = `text:...`:
    the string written to the resulting store path
  if `<type>` = `source`:
    the serialisation of the path from which this store path is
    copied, as returned by hashPath()
  if `<type>` = `output:<id>`:
    for non-fixed derivation outputs:
      the derivation (see hashDerivationModulo() in
      primops.cc)
    for paths copied by addToStore() or produced by fixed-output
    derivations:
      the string `fixed:out:<rec><algo>:<hash>:`, where
        `<rec>` = `r:` for recursive (path) hashes, or `` for flat
          (file) hashes
        `<algo>` = `md5`, `sha1` or `sha256`
        `<hash>` = base-16 representation of the path or flat hash of
          the contents of the path (or expected contents of the
          path for fixed-output derivations)

Note that since an output derivation has always type output, while
something added by addToStore can have type output or source depending
on the hash, this means that the same input can be hashed differently
if added to the store via addToStore or via a derivation, in the sha256
recursive case.

It would have been nicer to handle fixed-output derivations under
`source`, e.g. have something like `source:<rec><algo>`, but we're
stuck with this for now...

The main reason for this way of computing names is to prevent name
collisions (for security).  For instance, it shouldn't be feasible
to come up with a derivation whose output path collides with the
path for a copied source.  The former would have a `<s>` starting with
`output:out:`, while the latter would have a `<s>` starting with
`source:`.
