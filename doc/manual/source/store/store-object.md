## Store Object

A Nix store is a collection of *store objects* with *references* between them.
A store object consists of

  - A [file system object](./file-system-object.md) as data

  - A set of [store paths](./store-path.md) as references to store objects

### References

Store objects can refer to both other store objects and themselves.
References from a store object to itself are called *self-references*.

Store objects and their references form a directed graph, where the store objects are the vertices, and the references are the edges.
In particular, the edge corresponding to a reference is from the store object that contains the reference, and to the store object that the store path (which is the reference) refers to.

References other than a self-reference must not form a cycle.
The graph of references excluding self-references thus forms a [directed acyclic graph].

[directed acyclic graph]: @docroot@/glossary.md#gloss-directed-acyclic-graph

We can take the [transitive closure] of the references graph, in which any pair of store objects have an edge if a *path* of one or more references exists from the first to the second object.
(A single reference always forms a path which is one reference long, but longer paths may connect objects which have no direct reference between them.)
The *requisites* of a store object are all store objects reachable by paths of references which start with given store object's references.

[transitive closure]: https://en.wikipedia.org/wiki/Transitive_closure

We can also take the [transpose graph] of the references graph, where we reverse the orientation of all edges.
The *referrers* of a store object are the store objects that reference it.

[transpose graph]: https://en.wikipedia.org/wiki/Transpose_graph

One can also combine both concepts: taking the transitive closure of the transposed references graph.
The *referrers closure* of a store object are the store objects that can reach the given store object via paths of references.

> **Note**
>
> Care must be taken to distinguish between the intrinsic and extrinsic properties of store objects.
> We can create graphs from the store objects in a store, but the contents of the store is not, in general fixed, and may instead change over time.
>
> - The references of a store object --- the set of store paths called the references --- is a field of a store object, and thus intrinsic by definition.
>   Regardless of what store contains the store object in question, and what else that store may or may not contain, the references are the same.
>
> - The requisites of a store object are almost intrinsic --- some store paths do not precisely refer to a unique single store object.
>   Exactly what store object is being referenced, and what in turn *its* references are, depends on the store in question.
>   Different stores may disagree on what a given store path refers to.
>
> - The referrers of a store object are completely extrinsic, and depends solely on the store which contains that store object, not the store object itself.
>   Other store objects which refer to the store object in question may be added or removed from the store.

### Immutability

Store objects are [immutable](https://en.wikipedia.org/wiki/Immutable_object):
Once created, they do not change nor can any store object they reference be changed.

> **Note**
>
> Stores which support atomically deleting multiple store objects allow more flexibility while still upholding this property.

### Closure property

A store can only contain a store object if it also contains all the store objects it refers to.

> **Note**
>
> The "closure property" isn't meant to prohibit, for example, [lazy loading](https://en.wikipedia.org/wiki/Lazy_loading) of store objects.
> However, the "closure property" and immutability in conjunction imply that any such lazy loading ought to be deterministic.

### Store Object Metadata {#metadata}

[Store implementations](@docroot@/store/types/index.md) currently associate more information than described above with a store object.
Quite arguably some of this information doesn't belong here, because it conflates concerns.

Two such properties are sizes:

- [*NAR size*]{#nar-size}:
  The size in bytes of the store object's [file system object](./file-system-object.md), serialised as a [Nix Archive](./file-system-object/content-address.md#serial-nix-archive).

  This is an intrinsic property of the store object itself, and not of the way any particular store happens to store it.
  In particular, it is unrelated to the space the store object occupies on disk, which varies with compression, deduplication, and file system overheads.

- [*closure NAR size*]{#closure-nar-size}:
  The sum of the [*NAR size*](#nar-size) of every store object in the [closure] of the store object, including itself.

  Unlike the *NAR size*, this is not quite an intrinsic property.
  As a consequence of the [requisites](#references) being slightly ambiguous as described above, the *closure NAR size* is also slightly ambiguous;
  both depend on the store in question and the exact closure in that store.

  Because of these issues, implementations currently typically compute this on the fly rather than storing it.

For details see the [store object info](@docroot@/protocols/json/store-object-info.md) JSON format or the [narinfo](@docroot@/protocols/binary-cache/narinfo.md) format.

[closure]: @docroot@/glossary.md#gloss-closure
