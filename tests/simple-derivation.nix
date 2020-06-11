# This derivation doesn't depend from nixpkgs, so that we can build it during
# our test suite.

with import ./config.nix;

mkDerivation {
  name = "simple-test-derivation";
  builder = builtins.toFile "builder" "ln -s $input $out";
  # builder = "/home/carlo/code/obsidian/nix/tests/simple-derivation-builder.sh";
  input =
    builtins.fetchTarball("http://alpha.gnu.org/gnu/hello/hello-2.6.90.tar.gz");
}
