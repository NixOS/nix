{ nixpkgs ? <nixpkgs>, system ? builtins.currentSystem }:

with import nixpkgs { inherit system; };

let nix = nixUnstable; in

runCommandCC "nix-repl"
  { buildInputs = [ pkgconfig readline nix boehmgc ]; }
  ''
    mkdir -p $out/bin
    g++ -O3 -Wall -std=c++14 \
      -o $out/bin/nix-repl ${./nix-repl.cc} \
      $(pkg-config --cflags nix-main) \
      -lnixformat -lnixutil -lnixstore -lnixexpr -lnixmain -lreadline -lgc \
      -DNIX_VERSION=\"${(builtins.parseDrvName nix.name).version}\"
  ''
