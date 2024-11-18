# Abstract Derivation and Derived Reference

So far, we have covered "inert" store objects.
But the point of the Nix store layer is to be a build system.
Other system (like Git or IPFS) also store and transfer immutable data, but they don't concern themselves with *how* that data was created.
This is where Nix comes in.

Abstract derivations and derived references are a mutually recursive concepts that allow Nix to represent builds plans.
In particular, derivations are the nodes and derived references the edges of the graphs that are build plans

## The Base Model

### Abstract Derivations

Derivations represent individual build steps.
They take inputs and produce outputs.
The inputs can either be store objects, or the outputs of other derivations.
In this manner, derivations and their inputs form a directed acyclic graph, which is the build plan.

In pseudo-code:

```idris
data DerivedRef -- from below

data Derivation

type OutputName = String

inputs : Derivation -> Set DerivedRef

outputs : Derivation -> Set OutputName
```

**Inputs**
The inputs to the build step.
They are derived references, as described below.

**Outputs**
The names of the outputs produced by the build step.
The overall output is thus conceptually a "record" of name store objects.

::: {.note}
It may seem superfluous that multiple outputs can be returned, when a single store object with references to other store forms much the same thing.
It is indeed superfluous in the abstract, but makes sense for concrete Nix for reasons that will be given in the second half of this chapter on the concrete Nix store.
:::

Built steps are "atomic" in the sense that Nix doesn't care about any intermediate result between "success, produced all output" and "failed, produced no outputs".
Caching build plans at the granularity of individual build steps is *immensely* important, and the *raison d'être* of any build system.
Caching at any finer granularity by contrast is explicitly out of scope.
If you want finer-grained caching, break out your build plan into finer-grained derivations and outputs!

### Abstract Derived References

Derived references are a generalization of store object references to allow referring to existing or yet-to-be-built store objects.

In pseudo-code:

```idris
data DerivationRef -- reference to above

type OutputName = String

data DerivedRef
  = OpaqueObj { storeObj : StoreObjectRef }
  | BuiltObj {
      drv    : DerivationRef,
      output : OutputName,
    }
```

Two cases:

**Opaque references**
These are just store object references.
Per the *no dangling* rule already discussed, one can only refer directly like this to store objects that already exists.

**Built reference**
These are a pair of a derivation reference and an output name.
They indirectly refer to the store output with the given name that will be produced by the given derivation.
Since the store output is not directly referenced, the *no dangling* rule is impacted by this.
Thus, store objects that are not yet built can be referred to this way.

## Planning -- Execution separation

So far, we have implicitly assumed build plans are "static", which is to say they should exist prior to all building, and we shouldn't need to change them because they are building.
In fact, in the next section, we will rule out encoding `BuiltObj` by first building the selected output precisely because that would intermingle building and encoding.

This staticness is the heart of a "planning -- execution" separation.
We want finish planning (creating a build plan) before we execute any part of that plan, and
we want building to not require modifying the plan.

If this sounds sort of obvious --- since when in natural did carrying out a plan necessitate modifying that plan? -- great!
If you are familiar with other build systems that do not have this rigid separation, say those that decide what to do "on the fly" while doing the previous step, then this will section will in fact make sense as a distinguishing feature of Nix.

## Encoding Derivations as Store Objects

We so far haven't mentioned out build plans are *stored*.
One convenient approach would be to store them as store objects.

:::{.note}
As is often the case, it is useful to compare the abstract store model with abstract machine models at this point.
Without the build plan, the machine would be "inert", with no state transition.
If the build plan is stored separately, we have a "Harvard" architecture.
If the build plan is stored as store objects along with the input it "acts" on, we have a "Von Neumann" architecture.
:::

Build plans and store objects both form acyclic graphs, so the best encoding would preserve the build plan's graph structure in the store object graph.
When the input of the derivation is an `OpaqueObj` to store object, just make that store object a referenced by the encoded derivation.
But when the input of the derivation is a `BuiltObj`, what should we do?
We could build the referenced derivation and reference the selected output, but then our serialization depends on building and doesn't represent build plans faithfully.
Instead, we can somehow encode the output name (just a string), then recursively encode the referenced derivation, and finally reference that derivation *encoding*, a store object.
This makes the encoding a *graph homomorphism*, preserving the graph structure just as we wanted!

All put together, we have these properties

Suppose that we have an encoding of derivations as regular store objects.
In other words, we have a pair of functions:
```idris
decode : StoreObject -> Maybe Derivation
encode : Derivation -> StoreObject
```
that "round trip" in the expected way.

Then we can say, given a `ref : StoreObject -> StoreObjectRef`:

 - if `OpaqueObj { storeObj }` is a member of `inputs drv`,
   then `ref` is a member of `references (encode drv)`.

 - if `BuiltObj { drv, output }` is a member of `inputs drv`,
   then `ref (encode drv)` is a member of `references (encode drv)`.

## Extending the model to be higher-order

**Experimental feature**: `computed-derivations**
**RFC***: [92](https://github.com/NixOS/rfcs/pull/92)

Let's continue assuming we have a such an encoding from derivations to store objects as described above.
`decode` doesn't just work on the results of calling `encoding`: it works on any store object obeying the derivation encoding format (whatever that may be), regardless of how that store object was produced.
That means, it should work on suitable store objects produced as outputs of other derivations, too.

The careful reader will notice we seem to be on the verge of contradicting ourselves.
Just two sections previously, we just emphasized how Nix has a strict separation of planning.
But if we are to do something useful with derivation outputs decoded into derivations themselves, we surely must be unseparating those two phases of work, right?

:::{.note}
In the parlance of "Build Systems à la carte", we would are generalizing Nix to be an "Monadic" instead of "Applicative" build system.
:::

Well, yes.
But the truth is mixing the two does unlock significant more expressive power.
Most typically, if dependency information is already encoded in some other format, this allows us to combine an "pre-plan" extracting that information in order to build our "plan proper" and the plan proper into one complete higher-order plan.

All that said, how does it actually work?
For simplicity's sake, first assume that `DerivationRef` is `StoreObjectRef`; in other words, that we reference derivations solely by referencing their encoded form as store objects.
This need not by the case, but makes explaining what we are doing here simpler.

With that change, `DerivedRef` now looks like:

```idris
type OutputName = String

data DerivedRef
  = OpaqueObj { storeObj : StoreObjectRef }
  | BuiltObj {
      drv    : StoreObjectRef, -- changed
      output : OutputName,
    }
```

Now, since the foundation of our foray into higher-order build plans was the realization that we could also decode built store paths, instead say `DerivationRef` is `DerivedRef`.
`DerivedRef`, again, can refer to already-built and not-yet-built store objects a like, so using at as a derivation reference acknowledges the capability of decoding built store paths as derivations.

With that change, `DerivedRef` looks like:

```idris
type OutputName = String

data DerivedRef
  = OpaqueObj { storeObj : StoreObjectRef }
  | BuiltObj {
      drv    : DerivedRef, -- changed again
      output : OutputName,
    }
```

Now, `DerivedRef` is recursive.

### Examples

Let's do two examples

- What was written with the original first order `DerivedRef`
  ```idris
  BuiltObj {
    drv = drv0,
    output = output0,
  }
  ```
  now becomes
  ```idris
  BuiltObj {
    drv = OpaqueObj { storeObj = drv0 },
    output = output0,
  }
  ```
  This is a reference to an output named `output0` of a statically-known derivation `drv0`.

- ```idris
  BuiltObj {
    drv = BuiltObj {
      drv = OpaqueObj { storeObj = drv0 },
      output = output0
    },
    output0 = output1,
  }
  ```
  This is a reference to an output named `output0` of a dynamically computed derivation that is decoded from the output `output1` of drv `drv0`.
  What a mouthful!

### Alternative represenation

The above recursive type has the benefit of implying its meaning via its structure.
But, it might be hard to understand for those not familiar with such data types.
An alternative "flattend" representation that obscures the meaning, but is isomorphic and simpler is:

```idris
record DerivedRef where
  baseObj : StoreObjectRef
  outputs : List OutputName
```

We can then rewrite

- `OpaqueObj { storeObj }` as
   ```idris
   DerivedRef {
     baseObj = storeObj,
     outputs = [],
   }
   ```

- The first example above as
   ```idris
   DerivedRef {
     baseObj = drv0,
     outputs = [output0],
   }

- The second example above as
   ```idris
   DerivedRef {
     baseObj = drv0,
     outputs = [output0, output1],
   }
   ```

Processing left to right, every element in the `outputs` list means:

 1. interpret what we have so far as a derivation
 2. decode it and select its output named "current element"
