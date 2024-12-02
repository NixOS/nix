# Abstract Building

With abstract derivations and derived references now defined, we are almost ready to discuss building in the abstract.
But first we need one more concept.

## Basic Derivations

A "basic" derivation is one whose inputs are only allowed to be opaque store references.

```idris
d : BasicDerivation

OpaqueObj _ ∈ inputs d
BuiltObj  _ ∉ inputs d
```

The idea is that a basic derivation, unlike a derivation in general, only refers to concrete objects not recipes to build objects that may or may not have already been built.
A basic derivation is thus "ready to go".

## Two primitive operations

With that, we can move on to building proper.
All we need is two primitive operations for building: `substituteInput` and `build`.
Building is nothing but performing these operations!

### Build

```idris
type OutputName = String -- as before

buildDerivation : BasicDerivationRef -> Map OutputName StoreObjectRef
```

We previously described derivations as representing a single atomic step of building that Nix treats as a black box.
Here, we executes that single step.

This operation takes basic derivation because, as written above, basic derivations along are "ready to go".
*Inside* the black box --- however this is implemented --- we are aware of store object, but we are not necessary aware of derivations, derived references, or any other Nix machinery.
It an input is a built derived reference, we cannot assuredly resolve it to a store object.
(Remember, the built object being described by the built reference may not in fact exist yet,
Only the derivation must exist per the *no dangling* rule.)

With a basic derivation, however, we know that all inputs resolve to (already existing) store objects, we can gather up just those objects and their closure, and provide them to whatever the building mechanism is.
In this way builds are *hermetic* --- non-dependencies should not be visible to the build process in any way.

Building produces a store object for each output in the derivation, and so what we get back is the mapping from those output names to the store objects just built.

### Substitute Input

```idris
substituteInput : DerivedRef -> DerivedRef -> Derivation -> Derivation
```

What about those derivations that are not basic --- that have non-trivial inputs?

We cannot build those derivations directly, but we can recur on their inputs and try to build those.
The derivation graph must be acyclic and finite, so eventually we will reach a basic derivation we can build.
But then what?
How do we make progress back towards where we started?

What we need to do is substitute the inputs of derivations so we can replace `BuiltRef`s with `OpaqueRefs`, as we build their inputs and thus know exactly what store path they need (not just how to produce that store) path.

This is what this primitive does.

## Putting it all together

What we've described so far are the building blocks for a [*small-step operational semantics*].
This is a theoretical model for evaluation that allows it to occur in any order.
What many people want to do, however, is not merely build a few dependencies and stop, but build the entire thing.
The algorithm to evaluate completely is basically the same for any small-step operational semantics, but we present it for just this case below for completeness:

```idris
buildAll : DerivationRef -> Map OutputName StoreObjectRef
buildAll drv = build $ case tryIsBasic drv of
    Just basicDrv -> basicDrv
    Nothing -> buildInputs drv

buildInputs : DerivationRef -> BasicDerivationRef
buildInputs drv = case { (d, o) | BuiltRef d o <- inputs drv } of
  EmptySet       -> drv
  AtLeast (d, o) -> buildInputs $
    substituteInput (DerivedObj d o) (BuiltObj $ build d Map.!! o) drv
```

[small-step operational semantics]: https://en.wikipedia.org/wiki/Operational_semantics#Small-step_semantics
