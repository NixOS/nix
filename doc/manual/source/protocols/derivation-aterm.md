# Derivation "ATerm" file format

For historical reasons, [store derivations][store derivation] are stored on-disk in "Annotated Term" (ATerm) format
([guide](https://homepages.cwi.nl/~daybuild/daily-books/technology/aterm-guide/aterm-guide.html),
[paper](https://doi.org/10.1002/(SICI)1097-024X(200003)30:3%3C259::AID-SPE298%3E3.0.CO;2-Y)).

Only a small fragment of ATerm is used: the constructors below, string literals, lists, and tuples.

## The ATerm format used

Derivations are serialised in one of the following formats:

- ```
  Derive(...)
  ```

  For all stable derivations.

- ```
  DrvWithVersion(<version-string>, ...)
  ```

  The only `version-string`s that are in use today are for [experimental features](@docroot@/development/experimental-features.md):

  - `"xp-dyn-drv"` for the [`dynamic-derivations`](@docroot@/development/experimental-features.md#xp-feature-dynamic-derivations) experimental feature.

## Grammar

```ebnf
derivation = "Derive(" fields ")"
           | "DrvWithVersion(" version "," fields ")" ;

fields = outputs "," input-drvs "," input-srcs ","
         platform "," builder "," args "," env ;

outputs    = "[" [ output { "," output } ] "]" ;
output     = "(" output-name "," output-path "," hash-algo "," hash ")" ;

input-drvs = "[" [ input-drv { "," input-drv } ] "]" ;
input-drv  = "(" drv-path "," input-drv-node ")" ;

input-srcs = "[" [ store-path { "," store-path } ] "]" ;

builder    = string ;
args       = "[" [ string { "," string } ] "]" ;

env        = "[" [ env-entry { "," env-entry } ] "]" ;
env-entry  = "(" string "," string ")" ;
```

The leaves are all double-quoted, but fall into two kinds.

```ebnf
(* may contain escape sequences *)
string      = '"' { char | escape } '"' ;

(* drawn from restricted alphabets, so never escaped *)
verbatim    = '"' { char } '"' ;

(* Not an alternation but a parameter of the grammar: the store
   directory fixes which one holds, for the whole file at once.
   See the note below. *)
store-path  = ? verbatim under a Unix store directory,
                string under a Windows one ? ;

drv-path    = store-path ; (* a store path whose name ends in ".drv" *)
output-path = store-path | '""' ;
output-name = verbatim ;
platform    = verbatim ;
version     = verbatim ;

hash-algo     = '"' [ method-prefix ] algorithm '"' | '""' ;
method-prefix = "r:" | "text:" | "git:" ;      (* flat has none *)
algorithm     = "blake3" | "md5" | "sha1" | "sha256" | "sha512" ;

hash        = '"' hex-digit { hex-digit } '"' | '""' | '"impure"' ;
```

A `store-path` is a [store path](@docroot@/protocols/store-path.md) written in full, store directory included.

Note that the derivation *name* does not appear anywhere.
It is taken from the store path of the derivation, and so must be supplied out-of-band; see below.

> **Note**
>
> `store-path` is the one leaf that is not pinned down by the grammar, and the reason is the store directory.
> A Unix one, `/nix/store`, happens to hold no character the format escapes, so the path can be written verbatim.
> A Windows one does: `C:\ProgramData\nix\store` contains backslashes, which must be escaped, making the leaf a `string`:
>
> ```
> "C:\\ProgramData\\nix\\store/ib3sh3pcz10wsmavxvkdbayhqivbghlq-foo"
> ```
>
> Only the store directory is affected; the `/` separating it from the rest of the path is a forward slash either way.
> Nothing else moves: `output-name`, `platform`, `version` and the two hash fields are drawn from alphabets that never need escaping, whichever store directory is in use.
>
> This is a mode, not a choice made afresh at each store path: one of the two holds throughout a given file, and every store path in it is written that way.
> The mode is not recorded in the file and cannot be recovered from the bytes — a verbatim path is also a well-formed `string` — so it has to be supplied out of band, like the derivation name.
> Nix takes it from the store it is reading or writing for, defaulting to the platform it was built for, and then holds the parser to it: under a Unix store directory a store path carrying an escape is *rejected* rather than accepted as a second spelling of the same path.
> That is what keeps the encoding [canonical](#canonical-form) in either case.

### Strings

Every leaf above is a double-quoted string.
Within one, `"` and `\` are escaped with a backslash, and newline, carriage return, and tab are written `\n`, `\r`, and `\t`.
Those five are the only escapes there are, and each of those five characters is only ever written escaped; see [canonical form](#canonical-form).

Fields drawn from restricted alphabets — output names, the platform, and the hash fields — cannot contain any of them, and so are written verbatim.
Store paths are written verbatim too, unless the store directory itself needs escaping; see the note above.

### Outputs

Which sort of output is meant is not tagged; it is inferred from which of the three fields are empty.

| `output-path` | `hash-algo` | `hash` | output |
| --- | --- | --- | --- |
| set | empty | empty | [input-addressed](@docroot@/store/derivation/outputs/input-address.md) |
| empty | empty | empty | [deferred input-addressed](@docroot@/store/derivation/outputs/input-address.md#deferred) |
| set | set | set | [fixed-output content-addressed](@docroot@/store/derivation/outputs/content-address.md#fixed) |
| empty | set | empty | [floating content-addressed](@docroot@/store/derivation/outputs/content-address.md#floating) |
| empty | set | `"impure"` | [impure][xp-feature-impure-derivations] |

A deferred output does not use a kind of addressing of its own: it is input-addressed, but its path is not yet known, which happens downstream of a floating content-addressed input.
Nothing gates it.
Floating outputs require [`ca-derivations`][xp-feature-ca-derivations] and impure ones [`impure-derivations`][xp-feature-impure-derivations]; a fixed-output one additionally requires [`dynamic-derivations`][xp-feature-dynamic-derivations] if its method is `text:`.

The `method-prefix` of `hash-algo` is the [content addressing method](@docroot@/store/store-object/content-address.md):

| prefix | method |
| --- | --- |
| *(none)* | [Flat](@docroot@/store/store-object/content-address.md#method-flat) |
| `r:` | [Nix Archive](@docroot@/store/store-object/content-address.md#method-nix-archive) |
| `text:` | [Text](@docroot@/store/store-object/content-address.md#method-text) |
| `git:` | [Git](@docroot@/store/store-object/content-address.md#method-git) |

`hash` is lowercase, and carries no algorithm prefix of its own — the algorithm is named by `hash-algo`.

For a fixed-output derivation the `output-path` is redundant — it is a function of the content address — but it must still agree with it.
Two derivations that mean the same thing would otherwise have different encodings, and so different store paths.

### Input derivations

Each entry pairs the store path of an input derivation with the outputs of it that are used:

```ebnf
input-drv-node = output-names ;
output-names   = "[" [ output-name { "," output-name } ] "]" ;
```

In the `DrvWithVersion("xp-dyn-drv", ...)` form
— which requires the [`dynamic-derivations`][xp-feature-dynamic-derivations] experimental feature —
a node may instead nest, to name an output of a derivation that is *itself* an output of a derivation:

```ebnf
input-drv-node = output-names
               | "(" output-names "," "[" [ child { "," child } ] "]" ")" ;
child          = "(" output-name "," input-drv-node ")" ;
```

### Canonical form

The encoding is meant to be canonical: each derivation has exactly one.
This matters because the bytes are hashed, both to content-address the derivation itself and to compute [input-addressed](@docroot@/protocols/store-path.md) output paths, so two spellings of the same derivation would be two different derivations.

Accordingly:

- Outputs, input derivations, input sources, and environment variables appear in ascending order of their keys, with no duplicates.
  The same holds of the `output-names` naming which outputs of an input derivation are used.

- A character that can be escaped must be escaped, and no other escape sequence is valid.
  Both halves matter: a literal newline byte inside a string, and `\q`, are each a second way to write something that already has a spelling.

- The fields drawn from restricted alphabets — output names and the platform — contain no escape sequences at all, since an escape there would be a second encoding of the same value rather than a necessary one.
  Store paths are held to the same rule, except where the store directory needs escaping, in which case the escapes are the necessary kind.

- Store paths are written in their canonical form, and the key of an `inputDrvs` entry is a derivation path, ending in `.drv`.

- There is no trailing content after the closing parenthesis.

One exception is known.
The JSON held in the [`__json` environment variable](@docroot@/store/derivation/index.md#structured-attrs) of a derivation using structured attributes is *not* required to be canonical JSON.
It must instead be preserved verbatim rather than reserialised.
Since the bytes are hashed, whatever encoding a derivation happens to carry is part of its identity.
Re-emitting it — even as equivalent JSON — would change the derivation.

> **Note**
>
> This is not because canonicity is unwanted here, but because there is no settled notion of it to require.
> "Canonical JSON" has several competing definitions, disagreeing on key ordering, number representation, and Unicode escaping.
> It is also technically fraught:
> for example, without hex literals for floating point, number representation is barely tractable.
> Structured attributes have been in the wild for many years, so any such choice would also have to reckon with the encodings already out there.
> Committing to one of them is therefore not a choice we are yet in a position to make with this format.

## Masked derivations {#input-address-encoding}

An [input-addressed](@docroot@/store/derivation/outputs/input-address.md) output path is computed by hashing the ATerm representation of a derivation that has been [masked](@docroot@/store/derivation/outputs/input-address.md#input-masked-drv) first: parts of it are blanked out before hashing, so that derivations differing only in what was blanked hash alike.

Their encoding differs from the grammar above in two productions:

```ebnf
masked-derivation   = "Derive(" fields ")" ;

input-drv           = "(" drv-hash "," output-names ")" ;
drv-hash            = verbatim ;   (* lowercase hexadecimal *)
```

That is:

- There is no version header.
  A masked derivation is only ever constructed once dynamic derivations have been resolved away, so it never needs one, and `DrvWithVersion(...)` does not occur.

- An `inputDrvs` key is a hash rather than a store path, and its node is always the flat `output-names` — the nested form the `dynamic-derivations` version allows cannot appear, for the same reason.

See [input addressing](@docroot@/store/derivation/outputs/input-address.md#input-masked-drv) for how those hashes are derived and why; in short, they identify an input derivation up to what it produces rather than how, so that changing where a fixed-output input is fetched from does not change anything downstream.

Replacing the input derivation paths with hashes like this is *input masking*, and is performed on all derivations.

Whether the outputs carry their paths depends on what is being hashed.
When computing a derivation's own output paths they are *output masked* too — `output-path`, and the environment variable named after each output, are empty — since those paths are what is being computed; the result is *fully masked*.
When hashing a derivation to stand in for it as an *input* to another, they are left as they are, and the result is *input-masked* only.

The result is not a derivation that can be built, written to the store, or read back by `parse`: its inputs name hashes, not paths.
It exists only to be hashed.

## Use for encoding to store object

When derivation is encoded to a [store object] we make the following choices:

- The store path [name](@docroot@/store/store-path.md#name) is the derivation name with `.drv` suffixed at the end

  Indeed, the ATerm format above does *not* contain the name of the derivation, on the assumption that a store path will also be provided out-of-band.

- The derivation is content-addressed using the ["Text" method] of content-addressing derivations

Currently we always encode derivations to store object using the ATerm format (and the previous two choices),
but we reserve the option to encode new sorts of derivations differently in the future.

[store derivation]: @docroot@/glossary.md#gloss-store-derivation
[store object]: @docroot@/glossary.md#gloss-store-object
["Text" method]: @docroot@/store/store-object/content-address.md#method-text
[xp-feature-ca-derivations]: @docroot@/development/experimental-features.md#xp-feature-ca-derivations
[xp-feature-dynamic-derivations]: @docroot@/development/experimental-features.md#xp-feature-dynamic-derivations
[xp-feature-impure-derivations]: @docroot@/development/experimental-features.md#xp-feature-impure-derivations
