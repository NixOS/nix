{
  lib,
  config,
  nixpkgs,
  ...
}:

let
  pkgs = config.nodes.machine.nixpkgs.pkgs;
  pkgA = pkgs.hello;
  pkgB = pkgs.cowsay;
in
{
  name = "chroot-store";

  nodes = {
    machine =
      {
        config,
        lib,
        pkgs,
        ...
      }:
      {
        virtualisation.writableStore = true;
        virtualisation.additionalPaths = [ pkgA ];
        environment.systemPackages = [ pkgB ];
        nix.extraOptions = "experimental-features = nix-command";
      };
  };

  testScript =
    { nodes }:
    ''
      # fmt: off
      start_all()

      machine.succeed("nix copy --no-check-sigs --to /tmp/nix ${pkgA}")

      machine.succeed("nix shell --store /tmp/nix ${pkgA} --command hello >&2")

      # Test that /nix/store is available via an overlayfs mount.
      machine.succeed("nix shell --store /tmp/nix ${pkgA} --command cowsay foo >&2")

      # Building in /tmp should fail for security reasons.
      err = machine.fail("nix build --offline --store /tmp/nix --expr 'builtins.derivation { name = \"foo\"; system = \"x86_64-linux\"; builder = \"/foo\"; }' 2>&1")
      assert "is world-writable" in err
    '';
}
