test@{ lib, extendModules, ... }:
let
  inherit (lib) mkOption types;
in
{
  options = {
    quickBuild = mkOption {
      description = ''
        Whether to perform a "quick" build of the Nix package to test.

        When iterating on the functional tests, it's recommended to "set" this
        to `true`, so that changes to the functional tests don't require any
        recompilation of the package.
        You can do so by building the `.quickBuild` attribute on the check package,
        e.g:
        ```console
        nix build .#hydraJobs.functional_user.quickBuild
        ```

        We don't enable this by default to avoid the mostly unnecessary work of
        performing an additional build of the package in cases where we build
        the package normally anyway, such as in our pre-merge CI.
      '';
      type = types.bool;
      default = false;
    };
  };

  config = {
    passthru.quickBuild = 
      let withQuickBuild = extendModules { modules = [{ quickBuild = true; }]; };
      in withQuickBuild.config.test;

    defaults = { pkgs, ... }: {
      config = lib.mkIf test.config.quickBuild {
        nix.package = pkgs.nix_noTests;

        system.forbiddenDependenciesRegexes = [
          # This would indicate that the quickBuild feature is broken.
          # It could happen if NixOS has a dependency on pkgs.nix instead of
          # config.nix.package somewhere.
          (builtins.unsafeDiscardStringContext pkgs.nix.outPath)
        ];
      };
    };
  };
}