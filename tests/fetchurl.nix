{ filename, sha256 }:

import <nix/fetchurl.nix> {
  url = "file://${filename}";
  inherit sha256;
}
