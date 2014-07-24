{ nixpkgs ? <nixpkgs>, system ? builtins.currentSystem }:

with import nixpkgs { inherit system; };

runCommand "nix-repl"
  { buildInputs = [ readline nix boehmgc ]; }
  ''
    mkdir -p $out/bin
    g++ -O3 -Wall -std=c++0x \
      -o $out/bin/nix-repl ${./nix-repl.cc} \
      -I${nix}/include/nix \
      -lnixformat -lnixutil -lnixstore -lnixexpr -lnixmain -lreadline -lgc \
      -DNIX_VERSION=\"${(builtins.parseDrvName nix.name).version}\"
  ''
