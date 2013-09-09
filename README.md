nix-repl
========

`nix-repl` is a simple read–eval–print loop (REPL) for the Nix package
manager.

Installation
------------

Assuming you have Nix installed, just do

    $ git clone https://github.com/edolstra/nix-repl.git
    $ cd nix-repl
    $ nix-env -f . -i nix-repl

Example
-------

Here is a typical `nix-repl` session:

    $ nix-repl
    Welcome to Nix version 1.6pre3215_2c1ecf8. Type :? for help.

    nix-repl> 3 * 4
    12

    nix-repl> :l <nixpkgs>
    Added 3337 variables.

    nix-repl> lib.range 1 5
    [ 1 2 3 4 5 ]

    nix-repl> :a lib
    Added 299 variables.

    nix-repl> range 1 5
    [ 1 2 3 4 5 ]

    nix-repl> xs = range 1 5

    nix-repl> map (x: x * x) xs
    [ 1 4 9 16 25 ]

    nix-repl> :l <nixos>
    Added 7 variables.

    nix-repl> config.services.dhcpd
    { configFile = null; enable = false; extraConfig = ""; interfaces = [ ... ]; machines = [ ... ]; }

    nix-repl> :p config.services.dhcpd
    { configFile = null; enable = false; extraConfig = ""; interfaces = [ "eth0" ]; machines = [ ]; }

    nix-repl> config.fileSystems
    { "/" = { ... }; "/boot" = { ... }; }

    nix-repl> mapAttrsToList (n: v: v.device) config.fileSystems
    [ "/dev/disk/by-label/nixos" "/dev/disk/by-label/boot" ]

    nix-repl> :b libjson
    these derivations will be built:
      /nix/store/h910xqb36pysxcxkayb1zkr1zcvvk1zy-libjson_7.6.1.zip.drv
      /nix/store/iv0rdx08di0fg704zyxklkvdz6i96lm8-libjson-7.6.1.drv
    ...
    this derivation produced the following outputs:
      out -> /nix/store/ys6bvgfia81rjwqxjlgkwnx6jhsml8h9-libjson-7.6.1

    nix-repl> :t makeFontsConf
    a function

    nix-repl> :b makeFontsConf { fontDirectories = [ "${freefont_ttf}/share/fonts/truetype" ]; }
    ...
    this derivation produced the following outputs:
      out -> /nix/store/jkw848xj0gkbhmvxi0hwpnhzn2716v3c-fonts.conf

    nix-repl> :s pan
    # Builds dependencies of the ‘pan’ derivation, then starts a shell
    # in which the environment variables of the derivation are set

    [nix-shell:/tmp/nix-repl]$ echo $src
    /nix/store/0ibx15r02nnkwiclmfbpzrzjm2y204fh-pan-0.139.tar.bz2

    [nix-shell:/tmp/nix-repl]$ exit

    nix-repl>

Tab completion works on variables in scope and on attribute sets.  For
example:

    $ nix-repl  '<nixpkgs>' '<nixos>'
    Welcome to Nix version 1.6pre3215_2c1ecf8. Type :? for help.

    nix-repl> thunder<TAB> => thunderbird

    nix-repl> <TAB>
    Display all 3634 possibilities? (y or n)

    nix-repl> lib<TAB>
    Display all 291 possibilities? (y or n)

    nix-repl> xorg.libX<TAB>
    xorg.libXdamage  xorg.libXdmcp

    nix-repl> config.networking.use<TAB>
    config.networking.useDHCP   config.networking.usePredictableInterfaceNames
