---
synopsis: |-
  The `build-hook` setting's default is less useful when using `libnixstore` as a library
prs:
- 11178
---

*This is an obscure issue that only affects usage of the `libnixstore` library outside of the Nix executable.*

As part the ongoing [rewrite of the build system](https://github.com/NixOS/nix/issues/2503) to use [Meson](https://mesonbuild.com/), we are also switching to packaging individual Nix components separately (and building them in separate derivations).
This means that when building `libnixstore` we do not know where the Nix binaries will be installed --- `libnixstore` doesn't know about downstream consumers like the Nix binaries at all.

*This is also unrelated to the _`post`_-`build-hook`*, which is often used for pushing to a cache.*

This has a small adverse affect on remote building --- the `build-remote` executable that is specified from the [`build-hook`](@docroot@/command-ref/conf-file.md#conf-build-hook) setting will not be gotten from the (presumed) installation location, but instead looked up on the `PATH`.
This means that other applications linking `libnixstore` that wish to use remote building must arrange for the `nix` command to be on the PATH (or manually overriding `build-hook`) in order for that to work.

Long term we don't envision this being a downside, because we plan to [get rid of `build-remote` and the build hook setting entirely](https://github.com/NixOS/nix/issues/1221).
There is simply no need to add a second layer of remote-procedure-calling when we want to connect to a remote builder.
The build hook protocol did in principle support custom ways of remote building, but that can also be accomplished with a custom service for the ssh or daemon/ssh-ng protocols, or with a custom [store type](@docroot@/store/types/index.md) i.e. `Store` subclass. <!-- we normally don't mention classes, but consider that this release note is about a library use case -->

The Perl bindings no longer expose `getBinDir` either, since they libraries those bindings wrap no longer know the location of installed binaries as described above.
