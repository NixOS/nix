with import ./lib.nix;
with builtins;

assert isFunction (import <nix/fetchurl.nix>);

assert length __nixPath == 5;
assert length (filter (x: baseNameOf x.path == "dir4") __nixPath) == 1;

import <a.nix> + import <b.nix> + import <c.nix> + import <dir5/c.nix>
  + (let __nixPath = [ { path = ./dir2; } { path = ./dir1; } ]; in import <a.nix>)
