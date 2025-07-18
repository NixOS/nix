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

  test_sandbox_path_options = mkDerivation {
    name = "test-sandobx-path-options";
    buildCommand = ''
        (
          set -x
          # "/paths/readonly":    { "source": "'"$symlinkDir"', "options":["ro=rec"] },
          # "/paths/options":     { "source": "'"$symlinkDir"', "options":["noexec","nosuid","nodev"] },
          # "/paths/propagation": { "source": "'"$symlinkDir"', "options":["rslave"] }

          readonly=/paths/readonly
          opts=/paths/options
          propagate=/paths/propagation

          # Check if readonly path exists and is mounted read-only
          if [ -e "$readonly" ]; then
              cat /proc/self/mountinfo |
              while IFS= read -r line; do
                  case "$line" in
                      *" $readonly "*)
                          opt=$(printf %s "$line" | cut -d ' ' -f 6)
                          case "$opt" in
                              *ro*) echo "readonly: ro found";;
                          esac
                          ;;
                  esac
              done
          fi

          # Check for noexec, nosuid, nodev in options
          if [ -e "$opts" ]; then
              val=$(cat /proc/self/mountinfo |
                  while IFS= read -r line; do
                      case "$line" in
                          *" $opts "*)
                              echo "$line" | cut -d ' ' -f 6
                              break
                              ;;
                      esac
                  done
              )

              for x in noexec nosuid nodev; do
                  case "$val" in
                      *"$x"*) echo "$opts: $x found";;
                  esac
              done
          fi

          # Check if propagation is shared (master present in field 7)
          if [ -e "$propagate" ]; then
              cat /proc/self/mountinfo |
              while IFS= read -r line; do
                  case "$line" in
                      *" $propagate "*)
                          prop=$(printf %s "$line" | cut -d ' ' -f 7)
                          case "$prop" in
                              *master*) echo "propagate: master found";;
                          esac
                          ;;
                  esac
              done
          fi

          touch $out
      )
    '';
  };
}
