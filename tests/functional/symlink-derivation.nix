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
{
  depends_on_symlink = mkDerivation {
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

        # Native paths
      )
      echo "Success!" > $out
    '';
  };

  test_sandbox_paths = mkDerivation {
    # Depends on the caller to set a bunch of `--sandbox-path` arguments
    name = "test-sandbox-paths";
    buildCommand = ''
      (
        set -x
        [[ -f /file ]]
        [[ -d /dir ]]

        # /symlink and /symlinkDir should be available as raw symlinks
        # (pointing to files outside of the sandbox)
        [[ -L /symlink ]] && [[ ! -e $(readlink /symlink) ]]
        [[ -L /symlinkDir ]] && [[ ! -e $(readlink /symlinkDir) ]]
      )

      touch $out
    '';
  };
}
