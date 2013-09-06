with import <nixpkgs> { };

runCommand "nix-repl"
  { buildInputs = [ readline nixUnstable boehmgc ]; }
  ''
    mkdir -p $out/bin
    g++ -O3 -Wall -o $out/bin/nix-repl ${./nix-repl.cc} \
      -I${nixUnstable}/include/nix -L${nixUnstable}/lib/nix \
      -lexpr -lmain -lreadline -lgc
  ''
