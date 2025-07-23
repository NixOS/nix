# Release 2.25.0 (2024-11-07)

- New environment variables to override XDG locations [#11351](https://github.com/NixOS/nix/pull/11351)

  Added new environment variables:

  - `NIX_CACHE_HOME`
  - `NIX_CONFIG_HOME`
  - `NIX_DATA_HOME`
  - `NIX_STATE_HOME`

  Each, if defined, takes precedence over the corresponding [XDG environment variable](@docroot@/command-ref/env-common.md#xdg-base-directories).
  This provides more fine-grained control over where Nix looks for files. It allows having a stand-alone Nix environment that only uses files in a specific directory and that doesn't interfere with the user environment.

- Define integer overflow in the Nix language as an error [#10968](https://github.com/NixOS/nix/issues/10968) [#11188](https://github.com/NixOS/nix/pull/11188)

  Previously, integer overflow in the Nix language invoked C++ level signed overflow, which manifested as wrapping around on overflow. It now looks like this:

  ```
  $ nix eval --expr '9223372036854775807 + 1'
  error: integer overflow in adding 9223372036854775807 + 1
  ```

  Some other overflows were fixed:
  - `builtins.fromJSON` of values greater than the maximum representable value in a signed 64-bit integer will generate an error.
  - `nixConfig` in flakes will no longer accept negative values for configuration options.

- The `build-hook` setting no longer has a useful default when using `libnixstore` as a library [#11178](https://github.com/NixOS/nix/pull/11178)

  *This is an obscure issue that only affects usage of the `libnixstore` library outside of the Nix executable. It is unrelated to the `post-build-hook` settings, which is often used for pushing to a cache.*

  As part the ongoing [rewrite of the build system](https://github.com/NixOS/nix/issues/2503) to use [Meson](https://mesonbuild.com/), we are also switching to packaging individual Nix components separately (and building them in separate derivations).
  This means that when building `libnixstore` we do not know where the Nix binaries will be installed --- `libnixstore` doesn't know about downstream consumers like the Nix binaries at all.

  This has a small adverse affect on remote building --- the `build-remote` executable that is specified from the [`build-hook`](@docroot@/command-ref/conf-file.md#conf-build-hook) setting will not be gotten from the (presumed) installation location, but instead looked up on the `PATH`.
  This means that other applications linking `libnixstore` that wish to use remote building must arrange for the `nix` command to be on the PATH (or manually overriding `build-hook`) in order for that to work.

  Long term we don't envision this being a downside, because we plan to [get rid of `build-remote` and the build hook setting entirely](https://github.com/NixOS/nix/issues/1221).
  There should simply be no need to have an extra, intermediate layer of remote-procedure-calling when we want to connect to a remote builder.
  The build hook protocol did in principle support custom ways of remote building, but that can also be accomplished with a custom service for the ssh or daemon/ssh-ng protocols, or with a custom [store type](@docroot@/store/types/index.md) i.e. `Store` subclass. <!-- we normally don't mention classes, but consider that this release note is about a library use case -->

  The Perl bindings no longer expose `getBinDir` either, since the underlying C++ libraries those bindings wrap no longer know the location of installed binaries as described above.

- Wrap filesystem exceptions more correctly [#11378](https://github.com/NixOS/nix/pull/11378)

  With the switch to `std::filesystem` in different places, Nix started to throw `std::filesystem::filesystem_error` in many places instead of its own exceptions.
  As a result, Nix no longer generated error traces when (for example) listing a non-existing directory. It could also lead to crashes inside the Nix REPL.

  This version catches these types of exception correctly and wraps them into Nix's own exception type.

  Author: [**@Mic92**](https://github.com/Mic92)

- Add setting `fsync-store-paths` [#1218](https://github.com/NixOS/nix/issues/1218) [#7126](https://github.com/NixOS/nix/pull/7126)

  Nix now has a setting `fsync-store-paths` that ensures that new store paths are durably written to disk before they are registered as "valid" in Nix's database. This can prevent Nix store corruption if the system crashes or there is a power loss. This setting defaults to `false`.

  Author: [**@squalus**](https://github.com/squalus)

- Removing the default argument passed to the `nix fmt` formatter [#11438](https://github.com/NixOS/nix/pull/11438)

  The underlying formatter no longer receives the "." default argument when `nix fmt` is called with no arguments.

  This change was necessary as the formatter wasn't able to distinguish between
  a user wanting to format the current folder with `nix fmt .` or the generic
  `nix fmt`.

  The default behavior is now the responsibility of the formatter itself, and
  allows tools such as `treefmt` to format the whole tree instead of only the
  current directory and below.

  Author: [**@zimbatm**](https://github.com/zimbatm)

- `<nix/fetchurl.nix>` uses TLS verification [#11585](https://github.com/NixOS/nix/pull/11585)

  Previously `<nix/fetchurl.nix>` did not do TLS verification. This was because the Nix sandbox in the past did not have access to TLS certificates, and Nix checks the hash of the fetched file anyway. However, this can expose authentication data from `netrc` and URLs to man-in-the-middle attackers. In addition, Nix now in some cases (such as when using impure derivations) does *not* check the hash. Therefore we have now enabled TLS verification. This means that downloads by `<nix/fetchurl.nix>` will now fail if you're fetching from a HTTPS server that does not have a valid certificate.

  `<nix/fetchurl.nix>` is also known as the builtin derivation builder `builtin:fetchurl`. It's not to be confused with the evaluation-time function `builtins.fetchurl`, which was not affected by this issue.


## Contributors

This release was made possible by the following 58 contributors:

- 1444 [**(@0x5a4)**](https://github.com/0x5a4)
- Adrian Hesketh [**(@a-h)**](https://github.com/a-h)
- Aleksana [**(@Aleksanaa)**](https://github.com/Aleksanaa)
- Alyssa Ross [**(@alyssais)**](https://github.com/alyssais)
- Andrew Marshall [**(@amarshall)**](https://github.com/amarshall)
- Artemis Tosini [**(@artemist)**](https://github.com/artemist)
- Artturin [**(@Artturin)**](https://github.com/Artturin)
- Bjørn Forsman [**(@bjornfor)**](https://github.com/bjornfor)
- Brian McGee [**(@brianmcgee)**](https://github.com/brianmcgee)
- Brian McKenna [**(@puffnfresh)**](https://github.com/puffnfresh)
- Bryan Honof [**(@bryanhonof)**](https://github.com/bryanhonof)
- Cole Helbling [**(@cole-h)**](https://github.com/cole-h)
- Eelco Dolstra [**(@edolstra)**](https://github.com/edolstra)
- Eman Resu [**(@llakala)**](https://github.com/llakala)
- Emery Hemingway [**(@ehmry)**](https://github.com/ehmry)
- Emil Petersen [**(@leetemil)**](https://github.com/leetemil)
- Emily [**(@emilazy)**](https://github.com/emilazy)
- Geoffrey Thomas [**(@geofft)**](https://github.com/geofft)
- Gerg-L [**(@Gerg-L)**](https://github.com/Gerg-L)
- Ivan Tkachev
- Jacek Galowicz [**(@tfc)**](https://github.com/tfc)
- Jan Hrcek [**(@jhrcek)**](https://github.com/jhrcek)
- Jason Yundt [**(@Jayman2000)**](https://github.com/Jayman2000)
- Jeremy Kerfs [**(@jkerfs)**](https://github.com/jkerfs)
- Jeremy Kolb [**(@kjeremy)**](https://github.com/kjeremy)
- John Ericson [**(@Ericson2314)**](https://github.com/Ericson2314)
- Jonas Chevalier [**(@zimbatm)**](https://github.com/zimbatm)
- Jordan Justen [**(@jljusten)**](https://github.com/jljusten)
- Josh Heinrichs [**(@joshheinrichs-shopify)**](https://github.com/joshheinrichs-shopify)
- Jörg Thalheim [**(@Mic92)**](https://github.com/Mic92)
- Kevin Cox [**(@kevincox)**](https://github.com/kevincox)
- Michael Gallagher [**(@mjgallag)**](https://github.com/mjgallag)
- Michael [**(@michaelvanstraten)**](https://github.com/michaelvanstraten)
- Nikodem Rabuliński [**(@nrabulinski)**](https://github.com/nrabulinski)
- Noam Yorav-Raphael [**(@noamraph)**](https://github.com/noamraph)
- Onni Hakala [**(@onnimonni)**](https://github.com/onnimonni)
- Parker Hoyes [**(@parkerhoyes)**](https://github.com/parkerhoyes)
- Philipp Otterbein
- Pol Dellaiera [**(@drupol)**](https://github.com/drupol)
- Robert Hensing [**(@roberth)**](https://github.com/roberth)
- Ryan Hendrickson [**(@rhendric)**](https://github.com/rhendric)
- Sandro [**(@SuperSandro2000)**](https://github.com/SuperSandro2000)
- Seggy Umboh [**(@secobarbital)**](https://github.com/secobarbital)
- Sergei Zimmerman [**(@xokdvium)**](https://github.com/xokdvium)
- Shivaraj B H [**(@shivaraj-bh)**](https://github.com/shivaraj-bh)
- Siddhant Kumar [**(@siddhantk232)**](https://github.com/siddhantk232)
- Tim [**(@Jaculabilis)**](https://github.com/Jaculabilis)
- Tom Bereknyei
- Travis A. Everett [**(@abathur)**](https://github.com/abathur)
- Valentin Gagarin [**(@fricklerhandwerk)**](https://github.com/fricklerhandwerk)
- Vinayak Kaushik [**(@VinayakKaushikDH)**](https://github.com/VinayakKaushikDH)
- Yann Hamdaoui [**(@yannham)**](https://github.com/yannham)
- Yuriy Taraday [**(@YorikSar)**](https://github.com/YorikSar)
- bryango [**(@bryango)**](https://github.com/bryango)
- emhamm [**(@emhamm)**](https://github.com/emhamm)
- jade [**(@lf-)**](https://github.com/lf-)
- kenji [**(@a-kenji)**](https://github.com/a-kenji)
- pennae [**(@pennae)**](https://github.com/pennae)
- puckipedia [**(@puckipedia)**](https://github.com/puckipedia)
- squalus [**(@squalus)**](https://github.com/squalus)
- tomberek [**(@tomberek)**](https://github.com/tomberek)
