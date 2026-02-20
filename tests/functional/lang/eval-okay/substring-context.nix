with builtins;

let

  s = "${builtins.derivation {
    name = "test";
    builder = "/bin/sh";
    system = "x86_64-linux";
  }}";

in

if getContext s == getContext "${substring 0 0 s + unsafeDiscardStringContext s}" then
  "okay"
else
  throw "empty substring should preserve context"
