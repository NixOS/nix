# Verify that Linux builds cannot create setuid or setgid binaries.

{ nixpkgs, system, nix }:

with import (nixpkgs + "/nixos/lib/testing.nix") { inherit system; };

makeTest {

  machine =
    { config, lib, pkgs, ... }:
    { virtualisation.writableStore = true;
      nix.package = nix;
      nix.binaryCaches = [ ];
      nix.nixPath = [ "nixpkgs=${lib.cleanSource pkgs.path}" ];
      virtualisation.pathsInNixDB = [ pkgs.stdenv pkgs.pkgsi686Linux.stdenv ];
    };

  testScript = { nodes }:
    ''
      startAll;

      # Copying to /tmp should succeed.
      $machine->succeed('nix-build --no-sandbox -E \'(with import <nixpkgs> {}; runCommand "foo" {} "
        mkdir -p $out
        cp ${pkgs.coreutils}/bin/id /tmp/id
      ")\' ');

      $machine->succeed('[[ $(stat -c %a /tmp/id) = 555 ]]');

      $machine->succeed("rm /tmp/id");

      # Creating a setuid binary should fail.
      $machine->fail('nix-build --no-sandbox -E \'(with import <nixpkgs> {}; runCommand "foo" {} "
        mkdir -p $out
        cp ${pkgs.coreutils}/bin/id /tmp/id
        chmod 4755 /tmp/id
      ")\' ');

      $machine->succeed('[[ $(stat -c %a /tmp/id) = 555 ]]');

      $machine->succeed("rm /tmp/id");

      # Creating a setgid binary should fail.
      $machine->fail('nix-build --no-sandbox -E \'(with import <nixpkgs> {}; runCommand "foo" {} "
        mkdir -p $out
        cp ${pkgs.coreutils}/bin/id /tmp/id
        chmod 2755 /tmp/id
      ")\' ');

      $machine->succeed('[[ $(stat -c %a /tmp/id) = 555 ]]');

      $machine->succeed("rm /tmp/id");

      # The checks should also work on 32-bit binaries.
      $machine->fail('nix-build --no-sandbox -E \'(with import <nixpkgs> { system = "i686-linux"; }; runCommand "foo" {} "
        mkdir -p $out
        cp ${pkgs.coreutils}/bin/id /tmp/id
        chmod 2755 /tmp/id
      ")\' ');

      $machine->succeed('[[ $(stat -c %a /tmp/id) = 555 ]]');

      $machine->succeed("rm /tmp/id");

      # The tests above use fchmodat(). Test chmod() as well.
      $machine->succeed('nix-build --no-sandbox -E \'(with import <nixpkgs> {}; runCommand "foo" { buildInputs = [ perl ]; } "
        mkdir -p $out
        cp ${pkgs.coreutils}/bin/id /tmp/id
        perl -e \"chmod 0666, qw(/tmp/id) or die\"
      ")\' ');

      $machine->succeed('[[ $(stat -c %a /tmp/id) = 666 ]]');

      $machine->succeed("rm /tmp/id");

      $machine->fail('nix-build --no-sandbox -E \'(with import <nixpkgs> {}; runCommand "foo" { buildInputs = [ perl ]; } "
        mkdir -p $out
        cp ${pkgs.coreutils}/bin/id /tmp/id
        perl -e \"chmod 04755, qw(/tmp/id) or die\"
      ")\' ');

      $machine->succeed('[[ $(stat -c %a /tmp/id) = 555 ]]');

      $machine->succeed("rm /tmp/id");

      # And test fchmod().
      $machine->succeed('nix-build --no-sandbox -E \'(with import <nixpkgs> {}; runCommand "foo" { buildInputs = [ perl ]; } "
        mkdir -p $out
        cp ${pkgs.coreutils}/bin/id /tmp/id
        perl -e \"my \\\$x; open \\\$x, qw(/tmp/id); chmod 01750, \\\$x or die\"
      ")\' ');

      $machine->succeed('[[ $(stat -c %a /tmp/id) = 1750 ]]');

      $machine->succeed("rm /tmp/id");

      $machine->fail('nix-build --no-sandbox -E \'(with import <nixpkgs> {}; runCommand "foo" { buildInputs = [ perl ]; } "
        mkdir -p $out
        cp ${pkgs.coreutils}/bin/id /tmp/id
        perl -e \"my \\\$x; open \\\$x, qw(/tmp/id); chmod 04777, \\\$x or die\"
      ")\' ');

      $machine->succeed('[[ $(stat -c %a /tmp/id) = 555 ]]');

      $machine->succeed("rm /tmp/id");
    '';

}
