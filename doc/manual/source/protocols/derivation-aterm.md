# Derivation "ATerm" file format

For historical reasons, [store derivations][store derivation] are stored on-disk in "Annotated Term" (ATerm) format
([guide](https://homepages.cwi.nl/~daybuild/daily-books/technology/aterm-guide/aterm-guide.html),
[paper](https://doi.org/10.1002/(SICI)1097-024X(200003)30:3%3C259::AID-SPE298%3E3.0.CO;2-Y)).

Only a small fragment of ATerm is used: the constructors below, string literals, lists, and tuples.

## The ATerm format used

Derivations are serialised in one of the following formats, one for each [version](@docroot@/store/derivation/versions.md) of derivation:

- ```
  Derive(...)
  ```

  For [version 1] derivations, which include all stable derivations.

- ```
  DrvWithVersion("v2", ...)
  ```

  For [version 2] derivations, which are [experimental](./derivation-aterm/v2.md).

> **Note**
>
> `DrvWithVersion(...)` was previously used with the version string `"xp-dyn-drv"`, for derivations using the [`dynamic-derivations`][xp-feature-dynamic-derivations] experimental feature.
> Such derivations are now version 2 derivations, and that form is no longer used.

## Grammar

There is one grammar per version of derivation, each with its own entry point: [`derivation-v1`](./derivation-aterm/v1.md) and [`derivation-v2`](./derivation-aterm/v2.md).
Here we only define the productions that they both have in common;
Think of the rest of this page as a library of subgrammar building-blocks to be used in each of the version-specific grammars as they choose.

### Common productions {#common}

#### Combinators

Lists recur throughout, so the grammar uses a parameterised production for them:

```ebnf
list(x) = "[" [ x { "," x } ] "]" ;
```

This is not standard EBNF, which has no parameters, but a macro: `list(output)`, for example, stands for the right-hand side of `list` with `output` substituted for `x`.
Tuples of fixed arity are written out with `"("` and `")"`.

#### Leaves

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
output-name = verbatim ;
platform    = verbatim ;
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
> Nothing else moves: `output-name`, `platform` and the two hash fields are drawn from alphabets that never need escaping, whichever store directory is in use.
>
> This is a mode, not a choice made afresh at each store path: one of the two holds throughout a given file, and every store path in it is written that way.
> The mode is not recorded in the file and cannot be recovered from the bytes — a verbatim path is also a well-formed `string` — so it has to be supplied out of band, like the derivation name.
> Nix takes it from the store it is reading or writing for, defaulting to the platform it was built for, and then holds the parser to it: under a Unix store directory a store path carrying an escape is *rejected* rather than accepted as a second spelling of the same path.
> That is what keeps the encoding [canonical](#canonical-form) in either case.

#### Strings

Every leaf above is a double-quoted string.
Within one, `"` and `\` are escaped with a backslash, and newline, carriage return, and tab are written `\n`, `\r`, and `\t`.
Those five are the only escapes there are, and each of those five characters is only ever written escaped; see [canonical form](#canonical-form).

Fields drawn from restricted alphabets — output names, the platform, and the hash fields — cannot contain any of them, and so are written verbatim.
Store paths are written verbatim too, unless the store directory itself needs escaping; see the note above.

#### Shared fields

Both versions have the same input sources, platform, builder and arguments, and an output is, at its core, the same three fields in both:

```ebnf
input-srcs    = list(store-path) ;
builder       = string ;
args          = list(string) ;
output-names  = list(output-name) ;

output-fields = output-path "," hash-algo "," hash ;
output-path   = store-path | '""' ;
hash-algo     = '"' [ method-prefix ] algorithm '"' | '""' ;
method-prefix = "r:" | "text:" | "git:" ;      (* flat has none *)
algorithm     = "blake3" | "md5" | "sha1" | "sha256" | "sha512" ;
hash          = '"' hex-digit { hex-digit } '"' | '""' | '"impure"' ;
```

Which sort of output is meant is not tagged; it is inferred from which of the three fields are empty.
Each version allows only some sorts:

| `output-path` | `hash-algo` | `hash` | output | version |
| --- | --- | --- | --- | --- |
| set | empty | empty | [input-addressed](@docroot@/store/derivation/outputs/input-address.md), output paths known in advance | 1, 2 |
| set | set | set | [fixed-output content-addressed](@docroot@/store/derivation/outputs/content-address.md#fixed) | 1 |
| empty | empty | empty | [deferred input-addressed](@docroot@/store/derivation/outputs/input-address.md#deferred) | 2 |
| empty | set | empty | [floating content-addressed](@docroot@/store/derivation/outputs/content-address.md#floating) | 2 |
| empty | set | `"impure"` | [impure][xp-feature-impure-derivations] | 2 |

Floating outputs require [`ca-derivations`][xp-feature-ca-derivations] and impure ones [`impure-derivations`][xp-feature-impure-derivations]; a fixed-output one additionally requires [`dynamic-derivations`][xp-feature-dynamic-derivations] if its method is `text:`.

> **Note**
>
> Version 2 derivations do not yet support fixed-output derivations, but are meant to in the future.

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

#### Canonical form {#canonical-form}

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

[version 1]: @docroot@/store/derivation/versions.md#version-1
[version 2]: @docroot@/store/derivation/versions.md#version-2
