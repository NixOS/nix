# Release 1.11 (2016-01-19)

This is primarily a bug fix release. It also has a number of new
features:

  - `nix-prefetch-url` can now download URLs specified in a Nix
    expression. For example,
    
        $ nix-prefetch-url -A hello.src
    
    will prefetch the file specified by the `fetchurl` call in the
    attribute `hello.src` from the Nix expression in the current
    directory, and print the cryptographic hash of the resulting file on
    stdout. This differs from `nix-build -A
                    hello.src` in that it doesn't verify the hash, and is thus useful
    when you’re updating a Nix expression.
    
    You can also prefetch the result of functions that unpack a tarball,
    such as `fetchFromGitHub`. For example:
    
        $ nix-prefetch-url --unpack https://github.com/NixOS/patchelf/archive/0.8.tar.gz
    
    or from a Nix expression:
    
        $ nix-prefetch-url -A nix-repl.src

  - The builtin function `<nix/fetchurl.nix>` now supports downloading
    and unpacking NARs. This removes the need to have multiple downloads
    in the Nixpkgs stdenv bootstrap process (like a separate busybox
    binary for Linux, or curl/mkdir/sh/bzip2 for Darwin). Now all those
    files can be combined into a single NAR, optionally compressed using
    `xz`.

  - Nix now supports SHA-512 hashes for verifying fixed-output
    derivations, and in `builtins.hashString`.

  - The new flag `--option build-repeat
                            N` will cause every build to be executed *N*+1 times. If the build
    output differs between any round, the build is rejected, and the
    output paths are not registered as valid. This is primarily useful
    to verify build determinism. (We already had a `--check` option to
    repeat a previously succeeded build. However, with `--check`,
    non-deterministic builds are registered in the DB. Preventing that
    is useful for Hydra to ensure that non-deterministic builds don't
    end up getting published to the binary cache.)

  - The options `--check` and `--option
                            build-repeat N`, if they detect a difference between two runs of the
    same derivation and `-K` is given, will make the output of the other
    run available under `store-path-check`. This makes it easier to
    investigate the non-determinism using tools like `diffoscope`, e.g.,
    
        $ nix-build pkgs/stdenv/linux -A stage1.pkgs.zlib --check -K
        error: derivation ‘/nix/store/l54i8wlw2265…-zlib-1.2.8.drv’ may not
        be deterministic: output ‘/nix/store/11a27shh6n2i…-zlib-1.2.8’
        differs from ‘/nix/store/11a27shh6n2i…-zlib-1.2.8-check’
        
        $ diffoscope /nix/store/11a27shh6n2i…-zlib-1.2.8 /nix/store/11a27shh6n2i…-zlib-1.2.8-check
        …
        ├── lib/libz.a
        │   ├── metadata
        │   │ @@ -1,15 +1,15 @@
        │   │ -rw-r--r-- 30001/30000   3096 Jan 12 15:20 2016 adler32.o
        …
        │   │ +rw-r--r-- 30001/30000   3096 Jan 12 15:28 2016 adler32.o
        …

  - Improved FreeBSD support.

  - `nix-env -qa --xml --meta` now prints license information.

  - The maximum number of parallel TCP connections that the binary cache
    substituter will use has been decreased from 150 to 25. This should
    prevent upsetting some broken NAT routers, and also improves
    performance.

  - All "chroot"-containing strings got renamed to "sandbox". In
    particular, some Nix options got renamed, but the old names are
    still accepted as lower-priority aliases.

This release has contributions from Anders Claesson, Anthony Cowley,
Bjørn Forsman, Brian McKenna, Danny Wilson, davidak, Eelco Dolstra,
Fabian Schmitthenner, FrankHB, Ilya Novoselov, janus, Jim Garrison, John
Ericson, Jude Taylor, Ludovic Courtès, Manuel Jacob, Mathnerd314, Pascal
Wittmann, Peter Simons, Philip Potter, Preston Bennes, Rommel M.
Martinez, Sander van der Burg, Shea Levy, Tim Cuthbertson, Tuomas
Tynkkynen, Utku Demir and Vladimír Čunát.
