# Nix Store

The *Nix store* is an abstraction to store immutable file system data (such as software packages) that can have dependencies on other such data.

Concretely, albeit using concepts that are only defined in the rest of the chapter, a store consists of:

- A set of [store objects][store object], the immutable file system data.

  This can also be looked at as a map from [store paths][store path] to store objects.

- A set of [derivations][derivation], instructions for building store objects.

  This can also be looked at as a map from [store paths][store path] to derivations.
  Since store paths to derivations always end in `.drv`, and store paths to other store objects never do, the two maps can also be combined into one.
  Derivations can also be encoded as store objects too.

- A [build trace], a record of which derivations have been built and what they produced.

  > **Warning**
  >
  > The concept of a build trace is currently
  > [**experimental**](@docroot@/development/experimental-features.md#xp-feature-ca-derivations)
  > and subject to change.

There are [multiple types of Nix stores][store type] with different capabilities, such as the default one on the [local file system][local store] (`/nix/store`) or [binary caches][binary cache].

[store object]: ./store-object.md
[store path]: ./store-path.md
[derivation]: ./derivation/index.md
[build trace]: ./build-trace.md

[store type]: ./types/index.md
[local store]: ./types/local-store.md
[binary cache]: ./types/http-binary-cache-store.md
