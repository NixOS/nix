# Derivation Outputs and Types of Derivations

As stated on the [main pages on derivations](../index.md#store-derivation),
a derivation produces [store objects], which are known as the *outputs* of the derivation.
Indeed, the entire point of derivations is to produce these outputs, and to reliably and reproducably produce these derivations each time the derivation is run.

One of the parts of a derivation is its *outputs specification*, which specifies certain information about the outputs the derivation produces when run.
The outputs specification is a map, from names to specifications for individual outputs.

## Output Names {#outputs}

Output names can be any string which is also a valid [store path] name.
The name mapped to each output specification is not actually the name of the output.
In the general case, the output store object has name `derivationName + "-" + outputSpecName`, not any other metadata about it.
However, an output spec named "out" describes and output store object whose name is just the derivation name.

> **Example**
>
> A derivation is named `hello`, and has two outputs, `out`, and `dev`
>
> - The derivation's path will be: `/nix/store/<hash>-hello.drv`.
>
> - The store path of `out` will be: `/nix/store/<hash>-hello`.
>
> - The store path of `dev` will be: `/nix/store/<hash>-hello-dev`.

The outputs are the derivations are the [store objects][store object] it is obligated to produce.

> **Note**
>
> The formal terminology here is somewhat at adds with everyday communication in the Nix community today.
> "output" in casual usage tends to refer to either to the actual output store object, or the notional output spec, depending on context.
>
> For example "hello's `dev` output" means the store object referred to by the store path `/nix/store/<hash>-hello-dev`.
> It is unusual to call this the "`hello-dev` output", even though `hello-dev` is the actual name of that store object.

## Types of output addressing

The main information contained in an output specification is how the derivation output is addressed.
In particular, the specification decides:

- whether the output is [content-addressed](./content-address.md) or [input-addressed](./input-address.md)

- if the content is content-addressed, how is it content addressed

- if the content is content-addressed, [what is its content address](./content-address.md#fixed-content-addressing) (and thus what is its [store path])

## Output Checks

Additional checks for each output can also be mandated by the derivation,
supplementing the core required output specification above additional properties that must hold on the produced outputs for the derivation build to be considered successful.

**TODO No nix lang**

### Reference checks

The main checks assert properties about the [references][reference] of an output.
These checks vary on two different axes, yielding 4 possible checks.
The first axis is *direct* (references proper) vs *transitive* ([requisites]).
The first axis is *allowal* vs *disallowal*.

[reference]: @docroot@/glossary.md#gloss-reference

[requisites]: @docroot@/store/store-object.md#requisites

- [*allowed references*]{#allowed-references}: Set (store path or output name)

  The outputs references must be a subset of this set.
  Not every store path in the set must be a reference of the output,
  but every reference of the output must be in this set.

  For example, the empty set enforces that the output of a derivation cannot have any runtime dependencies on its inputs.

  > **Usage note**
  >
  > This is used in NixOS to check that generated files such as initial ramdisks for booting Linux don’t have accidental dependencies on other paths in the Nix store.

- [`allowedRequisites`]{#adv-attr-allowedRequisites}: Set (store paths or outputs name)

  like
  This attribute is similar to `allowedReferences`, but it specifies
  the legal requisites of the whole closure, so all the dependencies
  recursively. For example,

  ```nix
  allowedRequisites = [ foobar ];
  ```

  enforces that the output of a derivation cannot have any other
  runtime dependency than `foobar`, and in addition it enforces that
  `foobar` itself doesn't introduce any other dependency itself.

- [`disallowedReferences`]{#adv-attr-disallowedReferences}\
  The optional attribute `disallowedReferences` specifies a list of
  illegal references (dependencies) of the output of the builder. For
  example,

  ```nix
  disallowedReferences = [ foo ];
  ```

  enforces that the output of a derivation cannot have a direct
  runtime dependencies on the derivation `foo`.

  https://en.wikipedia.org/wiki/Blacklist_(computing)

- [`disallowedRequisites`]{#adv-attr-disallowedRequisites}\
  This attribute is similar to `disallowedReferences`, but it
  specifies illegal requisites for the whole closure, so all the
  dependencies recursively. For example,

  ```nix
  disallowedRequisites = [ foobar ];
  ```

  enforces that the output of a derivation cannot have any runtime
  dependency on `foobar` or any other derivation depending recursively
  on `foobar`.

The final references of the store object are always store paths.
However, if all elements of the sets above had to be store paths, it would be hard-to-impossible to write down the reference from outputs *to other outputs*, because in general we don't know outputs' store paths until they are built.

For this reason, it is also acceptable to use an output specification name (of the current derivation) instead of a store path.
  To allow an output to have a runtime
  dependency on itself, use `"out"` as a list item.

- [`outputChecks`]{#adv-attr-outputChecks}\
  When using [structured attributes](#adv-attr-structuredAttrs), the `outputChecks`
  attribute allows defining checks per-output.

  In addition to
  [`allowedReferences`](#adv-attr-allowedReferences), [`allowedRequisites`](#adv-attr-allowedRequisites),
  [`disallowedReferences`](#adv-attr-disallowedReferences) and [`disallowedRequisites`](#adv-attr-disallowedRequisites),
  the following attributes are available:

  - `maxSize` defines the maximum size of the resulting [store object](@docroot@/store/store-object.md).
  - `maxClosureSize` defines the maximum size of the output's closure.
  - `ignoreSelfRefs` controls whether self-references should be considered when
    checking for allowed references/requisites.

  Example:

  ```nix
  __structuredAttrs = true;

  outputChecks.out = {
    # The closure of 'out' must not be larger than 256 MiB.
    maxClosureSize = 256 * 1024 * 1024;

    # It must not refer to the C compiler or to the 'dev' output.
    disallowedRequisites = [ stdenv.cc "dev" ];
  };

  outputChecks.dev = {
    # The 'dev' output must not be larger than 128 KiB.
    maxSize = 128 * 1024;
  };
  ```

## Types of derivations

The sections on each type of derivation output addressing ended up discussing other attributes of the derivation besides its outputs, such as purity, scheduling, determinism, etc.
This is no concidence; for the type of a derivation is in fact one-for-one with the type of its outputs:

- A derivation that produces *xyz-addressed* outputs is an *xyz-addressing* derivations.

The rules for this are fairly concise:

- All the outputs must be of the same type / use the same addressing

  - The derivation must have at least one output

  - Additionally, if the outputs are fixed content-addressed, there must be exactly one output, whose specification is mapped from the name `out`.
    (The name `out` is special, according to the rules described above.
    Having only one output and calling its specification `out` means the single output is effectively anonymous; the store path just has the derivation name.)

    (This is an arbitrary restriction that could be lifted.)

- The output is either *fixed* or *floating*, indicating whether the its store path is known prior to building it.

  - With fixed content-addressing it is fixed.

    > A *fixed content-addressing* derivation is also called a *fixed-output derivation*, since that is the only currently-implemented form of fixed-output addressing

  - With floating content-addressing or input-addressing it is floating.

  > Thus, historically with Nix, with no experimental features enabled, *all* outputs are fixed.

- The derivation may be *pure* or *impure*, indicating what read access to the outside world the [builder](../index.md#builder) has.

  - An input-addressing derivation *must* be pure.

    > If it is impure, we would have a large problem, because an input-addressed derivation always produces outputs with the same paths.


  - A content-addressing derivation may be pure or impure

   - If it is impure, it may be be fixed (typical), or it may be floating if the additional [`impure-derivations`][xp-feature-impure-derivations] experimental feature is enabled.

   - If it is pure, it must be floating.

   - Pure, fixed content-addressing derivations are not suppported

     > There is no use for this forth combination.
     > The sole purpose of an output's store path being fixed is to support the derivation being impure.

[xp-feature-ca-derivations]: @docroot@/development/experimental-features.md#xp-feature-ca-derivations
[xp-feature-git-hashing]: @docroot@/development/experimental-features.md#xp-feature-git-hashing
[xp-feature-impure-derivations]: @docroot@/development/experimental-features.md#xp-feature-impure-derivations
