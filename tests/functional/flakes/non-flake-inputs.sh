#!/usr/bin/env bash

source ./common.sh

TODO_NixOS

createFlake1
createFlake2

nonFlakeDir=$TEST_ROOT/nonFlake
createGitRepo "$nonFlakeDir" ""

cat > "$nonFlakeDir/README.md" <<EOF
FNORD
EOF

git -C "$nonFlakeDir" add README.md
git -C "$nonFlakeDir" commit -m 'Initial'

flake3Dir=$TEST_ROOT/flake3
createGitRepo "$flake3Dir" ""

cat > "$flake3Dir/flake.nix" <<EOF
{
  inputs = {
    flake1 = {};
    flake2 = {};
    nonFlake = {
      url = git+file://$nonFlakeDir;
      flake = false;
    };
    nonFlakeFile = {
      url = path://$nonFlakeDir/README.md;
      flake = false;
    };
    nonFlakeFile2 = {
      url = "$nonFlakeDir/README.md";
      flake = false;
    };
  };

  description = "Fnord";

  outputs = inputs: rec {
    packages.$system.xyzzy = inputs.flake2.packages.$system.bar;
    packages.$system.sth = inputs.flake1.packages.$system.foo;
    packages.$system.fnord =
      with import ./config.nix;
      mkDerivation {
        inherit system;
        name = "fnord";
        dummy = builtins.readFile (builtins.path { name = "source"; path = ./.; filter = path: type: baseNameOf path == "config.nix"; } + "/config.nix");
        dummy2 = builtins.readFile (builtins.path { name = "source"; path = inputs.flake1; filter = path: type: baseNameOf path == "simple.nix"; } + "/simple.nix");
        buildCommand = ''
          cat \${inputs.nonFlake}/README.md > \$out
          [[ \$(cat \${inputs.nonFlake}/README.md) = \$(cat \${inputs.nonFlakeFile}) ]]
          [[ \${inputs.nonFlakeFile} = \${inputs.nonFlakeFile2} ]]
        '';
      };
  };
}
EOF

cp "${config_nix}" "$flake3Dir"

git -C "$flake3Dir" add flake.nix config.nix
git -C "$flake3Dir" commit -m 'Add nonFlakeInputs'

# Check whether `nix build` works with a lockfile which is missing a
# nonFlakeInputs.
nix build -o "$TEST_ROOT/result" "$flake3Dir#sth" --commit-lock-file

nix registry add --registry "$registry" flake3 "git+file://$flake3Dir"

nix build -o "$TEST_ROOT/result" flake3#fnord
[[ $(cat "$TEST_ROOT/result") = FNORD ]]

# Check whether flake input fetching is lazy: flake3#sth does not
# depend on flake2, so this shouldn't fail.
rm -rf "$TEST_HOME/.cache"
clearStore
mv "$flake2Dir" "$flake2Dir.tmp"
mv "$nonFlakeDir" "$nonFlakeDir.tmp"
nix build -o "$TEST_ROOT/result" flake3#sth
(! nix build -o "$TEST_ROOT/result" flake3#xyzzy)
(! nix build -o "$TEST_ROOT/result" flake3#fnord)
mv "$flake2Dir.tmp" "$flake2Dir"
mv "$nonFlakeDir.tmp" "$nonFlakeDir"
nix build -o "$TEST_ROOT/result" flake3#xyzzy flake3#fnord

# Make branch "removeXyzzy" where flake3 doesn't have xyzzy anymore
git -C "$flake3Dir" checkout -b removeXyzzy
rm "$flake3Dir/flake.nix"

cat > "$flake3Dir/flake.nix" <<EOF
{
  inputs = {
    nonFlake = {
      url = "$nonFlakeDir";
      flake = false;
    };
  };

  description = "Fnord";

  outputs = { self, flake1, flake2, nonFlake }: rec {
    packages.$system.sth = flake1.packages.$system.foo;
    packages.$system.fnord =
      with import ./config.nix;
      mkDerivation {
        inherit system;
        name = "fnord";
        buildCommand = ''
          cat \${nonFlake}/README.md > \$out
        '';
      };
  };
}
EOF
nix flake lock "$flake3Dir"
git -C "$flake3Dir" add flake.nix flake.lock
git -C "$flake3Dir" commit -m 'Remove packages.xyzzy'
git -C "$flake3Dir" checkout master

# Test whether fuzzy-matching works for registry entries.
nix registry add --registry "$registry" flake4 flake3
(! nix build -o "$TEST_ROOT/result" flake4/removeXyzzy#xyzzy)
nix build -o "$TEST_ROOT/result" flake4/removeXyzzy#sth
