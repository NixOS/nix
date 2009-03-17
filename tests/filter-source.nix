with import ./config.nix;

mkDerivation {
  name = "filter";
  builder = builtins.toFile "builder" "ln -s $input $out";
  input =
    let filter = path: type:
      type != "symlink"
      && baseNameOf path != "foo"
      && !((import ./lang/lib.nix).hasSuffix ".bak" (baseNameOf path));
    in builtins.filterSource filter ./test-tmp/filterin;
}
