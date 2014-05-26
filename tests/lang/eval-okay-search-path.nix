with import ./lib.nix;
with builtins;

assert pathExists <nix/buildenv.nix>;

assert length nixPath == 6;
assert length (filter (x: x.prefix == "nix") nixPath) == 1;
assert length (filter (x: baseNameOf x.path == "dir4") nixPath) == 1;

import <a.nix> + import <b.nix> + import <c.nix> + import <dir5/c.nix>
