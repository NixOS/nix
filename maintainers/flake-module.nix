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
            excludes = [
              # We haven't linted these files yet
              ''^config/install-sh$''
              ''^misc/bash/completion\.sh$''
              ''^misc/fish/completion\.fish$''
              ''^misc/zsh/completion\.zsh$''
              ''^scripts/create-darwin-volume\.sh$''
              ''^scripts/install-darwin-multi-user\.sh$''
              ''^scripts/install-multi-user\.sh$''
              ''^scripts/install-systemd-multi-user\.sh$''
              ''^src/nix/get-env\.sh$''
              ''^tests/functional/ca/build-dry\.sh$''
              ''^tests/functional/ca/build-with-garbage-path\.sh$''
              ''^tests/functional/ca/common\.sh$''
              ''^tests/functional/ca/concurrent-builds\.sh$''
              ''^tests/functional/ca/eval-store\.sh$''
              ''^tests/functional/ca/gc\.sh$''
              ''^tests/functional/ca/import-from-derivation\.sh$''
              ''^tests/functional/ca/new-build-cmd\.sh$''
              ''^tests/functional/ca/nix-shell\.sh$''
              ''^tests/functional/ca/post-hook\.sh$''
              ''^tests/functional/ca/recursive\.sh$''
              ''^tests/functional/ca/repl\.sh$''
              ''^tests/functional/ca/selfref-gc\.sh$''
              ''^tests/functional/ca/why-depends\.sh$''
              ''^tests/functional/characterisation-test-infra\.sh$''
              ''^tests/functional/common/vars-and-functions\.sh$''
              ''^tests/functional/completions\.sh$''
              ''^tests/functional/compute-levels\.sh$''
              ''^tests/functional/config\.sh$''
              ''^tests/functional/db-migration\.sh$''
              ''^tests/functional/debugger\.sh$''
              ''^tests/functional/dependencies\.builder0\.sh$''
              ''^tests/functional/dependencies\.sh$''
              ''^tests/functional/dump-db\.sh$''
              ''^tests/functional/dyn-drv/build-built-drv\.sh$''
              ''^tests/functional/dyn-drv/common\.sh$''
              ''^tests/functional/dyn-drv/dep-built-drv\.sh$''
              ''^tests/functional/dyn-drv/eval-outputOf\.sh$''
              ''^tests/functional/dyn-drv/old-daemon-error-hack\.sh$''
              ''^tests/functional/dyn-drv/recursive-mod-json\.sh$''
              ''^tests/functional/eval-store\.sh$''
              ''^tests/functional/export-graph\.sh$''
              ''^tests/functional/export\.sh$''
              ''^tests/functional/extra-sandbox-profile\.sh$''
              ''^tests/functional/fetchClosure\.sh$''
              ''^tests/functional/fetchGit\.sh$''
              ''^tests/functional/fetchGitRefs\.sh$''
              ''^tests/functional/fetchGitSubmodules\.sh$''
              ''^tests/functional/fetchGitVerification\.sh$''
              ''^tests/functional/fetchMercurial\.sh$''
              ''^tests/functional/fixed\.builder1\.sh$''
              ''^tests/functional/fixed\.builder2\.sh$''
              ''^tests/functional/fixed\.sh$''
              ''^tests/functional/flakes/absolute-paths\.sh$''
              ''^tests/functional/flakes/check\.sh$''
              ''^tests/functional/flakes/config\.sh$''
              ''^tests/functional/flakes/flakes\.sh$''
              ''^tests/functional/flakes/follow-paths\.sh$''
              ''^tests/functional/flakes/prefetch\.sh$''
              ''^tests/functional/flakes/run\.sh$''
              ''^tests/functional/flakes/show\.sh$''
              ''^tests/functional/formatter\.sh$''
              ''^tests/functional/formatter\.simple\.sh$''
              ''^tests/functional/gc-auto\.sh$''
              ''^tests/functional/gc-concurrent\.builder\.sh$''
              ''^tests/functional/gc-concurrent\.sh$''
              ''^tests/functional/gc-concurrent2\.builder\.sh$''
              ''^tests/functional/gc-non-blocking\.sh$''
              ''^tests/functional/hash-convert\.sh$''
              ''^tests/functional/impure-derivations\.sh$''
              ''^tests/functional/impure-eval\.sh$''
              ''^tests/functional/install-darwin\.sh$''
              ''^tests/functional/legacy-ssh-store\.sh$''
              ''^tests/functional/linux-sandbox\.sh$''
              ''^tests/functional/local-overlay-store/add-lower-inner\.sh$''
              ''^tests/functional/local-overlay-store/add-lower\.sh$''
              ''^tests/functional/local-overlay-store/bad-uris\.sh$''
              ''^tests/functional/local-overlay-store/build-inner\.sh$''
              ''^tests/functional/local-overlay-store/build\.sh$''
              ''^tests/functional/local-overlay-store/check-post-init-inner\.sh$''
              ''^tests/functional/local-overlay-store/check-post-init\.sh$''
              ''^tests/functional/local-overlay-store/common\.sh$''
              ''^tests/functional/local-overlay-store/delete-duplicate-inner\.sh$''
              ''^tests/functional/local-overlay-store/delete-duplicate\.sh$''
              ''^tests/functional/local-overlay-store/delete-refs-inner\.sh$''
              ''^tests/functional/local-overlay-store/delete-refs\.sh$''
              ''^tests/functional/local-overlay-store/gc-inner\.sh$''
              ''^tests/functional/local-overlay-store/gc\.sh$''
              ''^tests/functional/local-overlay-store/optimise-inner\.sh$''
              ''^tests/functional/local-overlay-store/optimise\.sh$''
              ''^tests/functional/local-overlay-store/redundant-add-inner\.sh$''
              ''^tests/functional/local-overlay-store/redundant-add\.sh$''
              ''^tests/functional/local-overlay-store/remount\.sh$''
              ''^tests/functional/local-overlay-store/stale-file-handle-inner\.sh$''
              ''^tests/functional/local-overlay-store/stale-file-handle\.sh$''
              ''^tests/functional/local-overlay-store/verify-inner\.sh$''
              ''^tests/functional/local-overlay-store/verify\.sh$''
              ''^tests/functional/logging\.sh$''
              ''^tests/functional/misc\.sh$''
              ''^tests/functional/multiple-outputs\.sh$''
              ''^tests/functional/nested-sandboxing\.sh$''
              ''^tests/functional/nested-sandboxing/command\.sh$''
              ''^tests/functional/nix-build\.sh$''
              ''^tests/functional/nix-channel\.sh$''
              ''^tests/functional/nix-collect-garbage-d\.sh$''
              ''^tests/functional/nix-copy-ssh-common\.sh$''
              ''^tests/functional/nix-copy-ssh-ng\.sh$''
              ''^tests/functional/nix-copy-ssh\.sh$''
              ''^tests/functional/nix-daemon-untrusting\.sh$''
              ''^tests/functional/nix-profile\.sh$''
              ''^tests/functional/nix-shell\.sh$''
              ''^tests/functional/nix_path\.sh$''
              ''^tests/functional/optimise-store\.sh$''
              ''^tests/functional/output-normalization\.sh$''
              ''^tests/functional/parallel\.builder\.sh$''
              ''^tests/functional/parallel\.sh$''
              ''^tests/functional/pass-as-file\.sh$''
              ''^tests/functional/path-from-hash-part\.sh$''
              ''^tests/functional/path-info\.sh$''
              ''^tests/functional/placeholders\.sh$''
              ''^tests/functional/post-hook\.sh$''
              ''^tests/functional/pure-eval\.sh$''
              ''^tests/functional/push-to-store-old\.sh$''
              ''^tests/functional/push-to-store\.sh$''
              ''^tests/functional/read-only-store\.sh$''
              ''^tests/functional/readfile-context\.sh$''
              ''^tests/functional/recursive\.sh$''
              ''^tests/functional/referrers\.sh$''
              ''^tests/functional/remote-store\.sh$''
              ''^tests/functional/repair\.sh$''
              ''^tests/functional/restricted\.sh$''
              ''^tests/functional/search\.sh$''
              ''^tests/functional/secure-drv-outputs\.sh$''
              ''^tests/functional/selfref-gc\.sh$''
              ''^tests/functional/shell\.shebang\.sh$''
              ''^tests/functional/simple\.builder\.sh$''
              ''^tests/functional/supplementary-groups\.sh$''
              ''^tests/functional/toString-path\.sh$''
              ''^tests/functional/user-envs-migration\.sh$''
              ''^tests/functional/user-envs-test-case\.sh$''
              ''^tests/functional/user-envs\.builder\.sh$''
              ''^tests/functional/user-envs\.sh$''
              ''^tests/functional/why-depends\.sh$''
            ];
          };
        };
      };
    };

  # We'll be pulling from this in the main flake
  flake.getSystem = getSystem;
}
