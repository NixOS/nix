# Derivation Outputs and Types of Derivations

As stated on the [main pages on derivations](../index.md#store-derivation),
a derivation produces [store objects](@docroot@/store/store-object.md), which are known as the *outputs* of the derivation.
Indeed, the entire point of derivations is to produce these outputs, and to reliably and reproducibly produce these derivations each time the derivation is run.

One of the parts of a derivation is its *outputs specification*, which specifies certain information about the outputs the derivation produces when run.
The outputs specification is a map, from names to specifications for individual outputs.

## Output Names {#outputs}

Output names can be any string which is also a valid [store path](@docroot@/store/store-path.md) name.
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

The outputs are the derivations are the [store objects](@docroot@/store/store-object.md) it is obligated to produce.

> **Note**
>
> The formal terminology here is somewhat at odds with everyday communication in the Nix community today.
> "output" in casual usage tends to refer to either to the actual output store object, or the notional output spec, depending on context.
>
> For example "hello's `dev` output" means the store object referred to by the store path `/nix/store/<hash>-hello-dev`.
> It is unusual to call this the "`hello-dev` output", even though `hello-dev` is the actual name of that store object.

## Types of output addressing

The main information contained in an output specification is how the derivation output is addressed.
In particular, the specification decides:

- whether the output is [content-addressed](./content-address.md) or [input-addressed](./input-address.md)

- if the content is content-addressed, how is it content addressed

- if the content is content-addressed, [what is its content address](./content-address.md#fixed) (and thus what is its [store path])

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

- The output is either *fixed* or *floating*, indicating whether the store path is known prior to building it.

  - With fixed content-addressing it is fixed.

    > A *fixed content-addressing* derivation is also called a *fixed-output derivation*, since that is the only currently-implemented form of fixed-output addressing

  - With floating content-addressing or input-addressing it is floating.

  > Thus, historically with Nix, with no experimental features enabled, *all* outputs are fixed.

- The derivation may be *pure* or *impure*, indicating what read access to the outside world the [builder](../index.md#builder) has.

  - An input-addressing derivation *must* be pure.

    > If it is impure, we would have a large problem, because an input-addressed derivation always produces outputs with the same paths.


  - A content-addressing derivation may be pure or impure

   - If it is impure, it may be fixed (typical), or it may be floating if the additional [`impure-derivations`][xp-feature-impure-derivations] experimental feature is enabled.

   - If it is pure, it must be floating.

   - Pure, fixed content-addressing derivations are not supported

     > There is no use for this forth combination.
     > The sole purpose of an output's store path being fixed is to support the derivation being impure.

[xp-feature-ca-derivations]: @docroot@/development/experimental-features.md#xp-feature-ca-derivations
[xp-feature-git-hashing]: @docroot@/development/experimental-features.md#xp-feature-git-hashing
[xp-feature-impure-derivations]: @docroot@/development/experimental-features.md#xp-feature-impure-derivations
