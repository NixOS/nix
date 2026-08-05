# Derivation Outputs and Types of Derivations

As stated on the [main pages on derivations](../index.md#store-derivation),
a derivation produces [store objects](@docroot@/store/store-object.md), which are known as the *outputs* of the derivation.
Indeed, the entire point of derivations is to produce these outputs, and to reliably and reproducibly produce these derivations each time the derivation is run.

One of the parts of a derivation is its *outputs specification*, which specifies certain information about the outputs the derivation produces when run.
The outputs specification is a map, from names to specifications for individual outputs.

## Output Names {#outputs}

Output names can be any string which is also a valid [store path name](@docroot@/store/store-path.md#name).
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

## Output checks {#output-checks}

In addition to the output specification above, a derivation may mandate additional *checks* on its outputs:
properties that must hold for the produced store objects in order for the build to be considered successful.
Checks may be specified separately for each output, or once for all outputs of the derivation.

### Reference checks

The main checks constrain the [references][reference] of an output.
These checks vary along two axes, yielding 4 possible checks:

- Whether the check applies to the *direct* references of the output, or to its entire [closure] via *transitive* references ([requisites][requisite]).

- Whether the check *allows* a set of references (every reference must be a member of the set, though not every member needs to be referenced), or *disallows* a set of references (no reference may be a member of the set).

The four checks are:

- [*allowed references*]{#allowed-references}:
  An optional set; the output's references must be a subset of it.
  When the set is absent the check is skipped, i.e. every reference is allowed.

  For example, the empty set enforces that the output has no runtime dependencies at all.

  > **Usage note**
  >
  > This is used in NixOS to check that generated files such as initial ramdisks for booting Linux don't have accidental dependencies on other paths in the Nix store.

- [*allowed requisites*]{#allowed-requisites}:
  Like *allowed references*, but applying to the whole closure of the output:
  every transitive dependency must be a member of the set.

- [*disallowed references*]{#disallowed-references}:
  A set of which no member may be a direct reference of the output.
  (There is no need for this check to be optional: disallowing nothing is the same as skipping the check.)

- [*disallowed requisites*]{#disallowed-requisites}:
  Like *disallowed references*, but applying to the whole closure of the output:
  no member of the set may appear anywhere in the output's transitive dependencies.

The references of a store object are always store paths.
However, if every element of these sets had to be a store path, it would be hard-to-impossible to constrain references from outputs *to other outputs* of the same derivation, because in general the store paths of outputs are not known until the derivation is built.
For this reason, an element of these sets may also be an *output name* of the derivation being checked, standing for the store path of that output, whatever it turns out to be.
For example, an output can be permitted to reference itself by including its own name in its *allowed references*.

### Size checks

- [*max size*]{#max-size}:
  The [NAR size][nar size] of the output's store object may not exceed the given number of bytes.

- [*max closure size*]{#max-closure-size}:
  The [closure NAR size][closure nar size] of the output's store object may not exceed the given number of bytes.

### Self-reference handling

- [*ignore self references*]{#ignore-self-refs}:
  Whether references of an output to itself are exempted from the reference checks above.

## Reference scanning {#reference-scanning}

Besides the output checks, there is one more per-output setting.
It is *not* a check — nothing is verified about the produced store object — but it is configured in the same per-output manner, so it is documented here:

- [*unsafe discard references*]{#unsafe-discard-references}:
  Disables [scanning the output for run-time references](../../building.md#scanning-for-references) altogether.
  The output is then registered as having no references, regardless of its contents.

  This is useful, for example, when generating self-contained filesystem images with their own embedded Nix store:
  hashes found inside such an image refer to the embedded store and not to the host's Nix store.

  As the name suggests, this is unsafe: discarding references that are in fact needed at run time makes the closure incomplete, so copying the output elsewhere will not bring its dependencies along.

[closure]: @docroot@/glossary.md#gloss-closure
[nar size]: @docroot@/store/store-object.md#nar-size
[closure nar size]: @docroot@/store/store-object.md#closure-nar-size
[reference]: @docroot@/glossary.md#gloss-reference
[requisite]: @docroot@/store/store-object.md#references

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
