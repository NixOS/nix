with import ./lib.nix;
with builtins;

assert pathExists <nix/buildenv.nix>;

assert length __nixPath == 6;
assert length (filter (x: x.prefix == "nix") __nixPath) == 1;
assert length (filter (x: baseNameOf x.path == "dir4") __nixPath) == 1;

import <a.nix> + import <b.nix> + import <c.nix> + import <dir5/c.nix>
  + (let __nixPath = [ { path = ./dir2; } { path = ./dir1; } ]; in import <a.nix>)
