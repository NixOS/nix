assert builtins.pathExists <nix/buildenv>;

import <a.nix> + import <b.nix> + import <c.nix> + import <dir5/c.nix>
