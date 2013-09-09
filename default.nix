{ nixpkgs ? <nixpkgs>, system ? builtins.currentSystem }:

with import nixpkgs { inherit system; };

runCommand "nix-repl"
  { buildInputs = [ readline nixUnstable boehmgc ]; }
  ''
    mkdir -p $out/bin
    g++ -O3 -Wall -std=c++0x \
      -o $out/bin/nix-repl ${./nix-repl.cc} \
      -I${nixUnstable}/include/nix -L${nixUnstable}/lib/nix \
      -lformat -lutil -lstore -lexpr -lmain -lreadline -lgc
  ''
