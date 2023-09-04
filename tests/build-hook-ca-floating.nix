{ busybox }:

import ./build-hook.nix {
  inherit busybox;
  contentAddressed = true;
}
