{
  lib,
  self,
  nixpkgsFor,
  devFlake,
  forAllSystems,
}:
forAllSystems (
  system:
  let
    pkgs = nixpkgsFor.${system}.native;
    opener = if pkgs.stdenv.isDarwin then "open" else "xdg-open";
  in
  {
    open-manual = {
      type = "app";
      program = "${pkgs.writeShellScript "open-nix-manual" ''
        path="${self.packages.${system}.nix-manual.site}/index.html"
        if ! ${opener} "$path"; then
          echo "Failed to open manual with ${opener}. Manual is located at:"
          echo "$path"
        fi
      ''}";
      meta.description = "Open the Nix manual in your browser";
    };

    format =
      let
        modular = devFlake.getSystem system;
        configFile =
          (pkgs.formats.yaml { }).generate "pre-commit-config.yaml"
            modular.pre-commit.settings.rawConfig;
      in
      {
        type = "app";
        program = "${lib.getExe (
          pkgs.writeShellApplication {
            name = "format-nix";
            runtimeInputs = [
              modular.pre-commit.settings.package
              pkgs.git
            ];
            text = ''
              export _NIX_PRE_COMMIT_HOOKS_CONFIG="${configFile}"
              max_attempts=10
              attempt=0
              while ! pre-commit run --config "$_NIX_PRE_COMMIT_HOOKS_CONFIG" --all-files; do
                  if [ "''${1:-}" = "--single-run" ]; then
                      exit 1
                  fi
                  attempt=$((attempt + 1))
                  if [ "$attempt" -ge "$max_attempts" ]; then
                      echo "format: still not clean after $max_attempts attempts, giving up" >&2
                      exit 1
                  fi
                  echo "format: re-running (attempt $((attempt + 1))/$max_attempts)..." >&2
              done
            '';
          }
        )}";
        meta.description = "Run all formatters on the tree";
      };
  }
)
