# Release 2.28.0 (2025-04-02)

This is an atypical release, and for almost all intents and purposes, it is just a continuation of 2.27; not a feature release.

We had originally set the goal of making 2.27 the Nixpkgs default for NixOS 25.05, but dependents that link to Nix need certain _interface breaking_ changes in the C++ headers. This is not something we should do in a patch release, so this is why we branched 2.28 right off 2.27 instead of `master`.

This completes the infrastructure overhaul for the [RFC 132](https://github.com/NixOS/rfcs/blob/master/rfcs/0132-meson-builds-nix.md) switchover to meson as our build system.

## Major changes

- Unstable C++ API reworked
  [#12836](https://github.com/NixOS/nix/pull/12836)
  [#12798](https://github.com/NixOS/nix/pull/12798)
  [#12773](https://github.com/NixOS/nix/pull/12773)

  Now the C++ interface confirms to common conventions much better than before:

  - All headers are expected to be included with the initial `nix/`, e.g. as `#include "nix/....hh"` (what Nix's headers now do) or `#include <nix/....hh>` (what downstream projects may choose to do).
    Likewise, the pkg-config files have `-I${includedir}` not `-I${includedir}/nix` or similar.

    Including without the `nix/` like before sometimes worked because of how for `#include` C pre-process checks the directory containing the current file, not just the lookup path, but this was not reliable.

  - All configuration headers are included explicitly by the (regular) headers that need them.
    There is no more need to pass `-include` to force additional files to be included.

  - The public, installed configuration headers no longer contain implementation-specific details that are not relevant to the API.
    The vast majority of definitions that were previously in there are now moved to new headers that are not installed, but used during Nix's own compilation only.
    The remaining macro definitions are renamed to have `NIX_` as a prefix.

  - The name of the Nix component the header comes from
    (e.g. `util`, `store`, `expr`, `flake`, etc.)
    is now part of the path to the header, coming after `nix` and before the header name
    (or rest of the header path, if it is already in a directory).

  Here is a contrived diff showing a few of these changes at once:

  ```diff
  @@ @@
  -#include "derived-path.hh"
  +#include "nix/store/derived-path.hh"
  @@ @@
  +// Would include for the variables used before. But when other headers
  +// need these variables. those will include these config themselves.
  +#include "nix/store/config.hh"
  +#include "nix/expr/config.hh"
  @@ @@
  -#include "config.hh"
  +// Additionally renamed to distinguish from components' config headers.
  +#include "nix/util/configuration.hh"
  @@ @@
  -#if HAVE_ACL_SUPPORT
  +#if NIX_SUPPORT_ACL
  @@ @@
  -#if HAVE_BOEHMGC
  +#if NIX_USE_BOEHMGC
  @@ @@
   #endif
   #endif
  @@ @@
  -const char *s = "hi from " SYSTEM;
  +const char *s = "hi from " NIX_LOCAL_SYSTEM;
  ```

- C API `nix_flake_init_global` removed [#5638](https://github.com/NixOS/nix/issues/5638) [#12759](https://github.com/NixOS/nix/pull/12759)

  In order to improve the modularity of the code base, we are removing a use of global state, and therefore the `nix_flake_init_global` function.

  Instead, use `nix_flake_settings_add_to_eval_state_builder`.
  For example:

  ```diff
  -    nix_flake_init_global(ctx, settings);
  -    HANDLE_ERROR(ctx);
  -
       nix_eval_state_builder * builder = nix_eval_state_builder_new(ctx, store);
       HANDLE_ERROR(ctx);

  +    nix_flake_settings_add_to_eval_state_builder(ctx, settings, builder);
  +    HANDLE_ERROR(ctx);
  ```

  Although this change is not as critical, we figured it would be good to do this API change at the same time, also.
  Also note that we try to keep the C API compatible, but we decided to break this function because it was young and likely not in widespread use yet. This frees up time to make important progress on the rest of the C API.

## Contributors

This earlier-than-usual release was made possible by the following 16 contributors:

- Farid Zakaria [**(@fzakaria)**](https://github.com/fzakaria)
- Jörg Thalheim [**(@Mic92)**](https://github.com/Mic92)
- Eelco Dolstra [**(@edolstra)**](https://github.com/edolstra)
- Graham Christensen [**(@grahamc)**](https://github.com/grahamc)
- Thomas Miedema [**(@thomie)**](https://github.com/thomie)
- Brian McKenna [**(@puffnfresh)**](https://github.com/puffnfresh)
- Sergei Trofimovich [**(@trofi)**](https://github.com/trofi)
- Dmitry Bogatov [**(@KAction)**](https://github.com/KAction)
- Erik Nygren [**(@Kirens)**](https://github.com/Kirens)
- John Ericson [**(@Ericson2314)**](https://github.com/Ericson2314)
- Sergei Zimmerman [**(@xokdvium)**](https://github.com/xokdvium)
- Ruby Rose [**(@oldshensheep)**](https://github.com/oldshensheep)
- Robert Hensing [**(@roberth)**](https://github.com/roberth)
- jade [**(@lf-)**](https://github.com/lf-)
- Félix [**(@picnoir)**](https://github.com/picnoir)
- Valentin Gagarin [**(@fricklerhandwerk)**](https://github.com/fricklerhandwerk)
- Dmitry Bogatov
