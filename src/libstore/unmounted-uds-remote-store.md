R"(

**Store URL format**: `unmounted-unix://`*path*

This store type accesses a Nix store by talking to a Nix daemon listening on the Unix domain socket *path*,
but without any assumption that the store is mounted on the local file system.

This is useful for local proxying daemon used to communicate with some other implementation.
For example, one could run a daemon which is supposed to authenticate and proxy with some sort of custom binary cache.

> **Note**

> It is unfortunately that we need a separate scheme (and C++ class) for this, because of the current architecture of swappable store implementations.
> Ideally, the mounted/unmounted choice can just be an option on the existing `unix://` scheme.
> This new scheme will probably remain experimental as a temporary stop-gap until we can do the ideal thing we actually want.
)"
