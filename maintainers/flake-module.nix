{
  lib,
  getSystem,
  inputs,
  ...
}:

{
  imports = [
    inputs.git-hooks-nix.flakeModule
  ];

  perSystem =
    { config, pkgs, ... }:
    {

      # https://flake.parts/options/git-hooks-nix#options
      pre-commit.settings = {
        hooks = {
          # Conflicts are usually found by other checks, but not those in docs,
          # and potentially other places.
          check-merge-conflicts.enable = true;
          # built-in check-merge-conflicts seems ineffective against those produced by mergify backports
          check-merge-conflicts-2 = {
            enable = true;
            entry = "${pkgs.writeScript "check-merge-conflicts" ''
              #!${pkgs.runtimeShell}
              conflicts=false
              for file in "$@"; do
                if grep --with-filename --line-number -E '^>>>>>>> ' -- "$file"; then
                  conflicts=true
                fi
              done
              if $conflicts; then
                echo "ERROR: found merge/patch conflicts in files"
                exit 1
              fi
            ''}";
          };
          meson-format =
            let
              meson = pkgs.meson.overrideAttrs {
                doCheck = false;
                doInstallCheck = false;
                patches = [
                  (pkgs.fetchpatch {
                    url = "https://github.com/mesonbuild/meson/commit/38d29b4dd19698d5cad7b599add2a69b243fd88a.patch";
                    hash = "sha256-PgPBvGtCISKn1qQQhzBW5XfknUe91i5XGGBcaUK4yeE=";
                  })
                ];
              };
            in
            {
              enable = true;
              files = "(meson.build|meson.options)$";
              entry = "${pkgs.writeScript "format-meson" ''
                #!${pkgs.runtimeShell}
                for file in "$@"; do
                  ${lib.getExe meson} format -ic ${../meson.format} "$file"
                done
              ''}";
            };
          nixfmt-rfc-style = {
            enable = true;
            excludes = [
              # Invalid
              ''^tests/functional/lang/parse-.*\.nix$''

              # Formatting-sensitive
              ''^tests/functional/lang/eval-okay-curpos\.nix$''
              ''^tests/functional/lang/.*comment.*\.nix$''
              ''^tests/functional/lang/.*newline.*\.nix$''
              ''^tests/functional/lang/.*eol.*\.nix$''

              # Syntax tests
              ''^tests/functional/shell.shebang\.nix$''
              ''^tests/functional/lang/eval-okay-ind-string\.nix$''

              # Not supported by nixfmt
              ''^tests/functional/lang/eval-okay-deprecate-cursed-or\.nix$''
              ''^tests/functional/lang/eval-okay-attrs5\.nix$''

              # More syntax tests
              # These tests, or parts of them, should have been parse-* test cases.
              ''^tests/functional/lang/eval-fail-eol-2\.nix$''
              ''^tests/functional/lang/eval-fail-path-slash\.nix$''
              ''^tests/functional/lang/eval-fail-toJSON-non-utf-8\.nix$''
              ''^tests/functional/lang/eval-fail-set\.nix$''
            ];
          };
          clang-format = {
            enable = true;
            # https://github.com/cachix/git-hooks.nix/pull/532
            package = pkgs.llvmPackages_latest.clang-tools;
            excludes = [
              # We don't want to format test data
              # ''tests/(?!nixos/).*\.nix''
              ''^src/[^/]*-tests/data/.*$''

              # Don't format vendored code
              ''^doc/manual/redirects\.js$''
              ''^doc/manual/theme/highlight\.js$''
            ];
          };
          shellcheck = {
            enable = true;
          };
        };
      };
    };

  # We'll be pulling from this in the main flake
  flake.getSystem = getSystem;
}
