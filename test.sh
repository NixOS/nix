make -j $NIX_BUILD_CORES
make install

./outputs/out/bin/nix flake show --extra-experimental-features nix-command --extra-experimental-features flakes
