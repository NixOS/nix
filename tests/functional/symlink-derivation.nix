with import ./config.nix;

let
  foo_in_store = builtins.toFile "foo" "foo";
  foo_symlink = mkDerivation {
    name = "foo-symlink";
    buildCommand = ''
      ln -s ${foo_in_store} $out
    '';
  };
  symlink_to_not_in_store = mkDerivation {
    name = "symlink-to-not-in-store";
    buildCommand = ''
      ln -s ${builtins.toString ./.} $out
    '';
  };
in
mkDerivation {
  name = "depends-on-symlink";
  buildCommand = ''
    (
      set -x

      # `foo_symlink` should be a symlink pointing to `foo_in_store`
      [[ -L ${foo_symlink} ]]
      [[ $(readlink ${foo_symlink}) == ${foo_in_store} ]]

      # `symlink_to_not_in_store` should be a symlink pointing to `./.`, which
      # is not available in the sandbox
      [[ -L ${symlink_to_not_in_store} ]]
      [[ $(readlink ${symlink_to_not_in_store}) == ${builtins.toString ./.} ]]
      (! ls ${symlink_to_not_in_store}/)
    )
    echo "Success!" > $out
  '';
}
