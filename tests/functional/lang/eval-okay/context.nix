let
  s = "foo ${builtins.substring 33 100 (baseNameOf "${./context.nix}")} bar";
in
if s != "foo context.nix bar" then
  abort "context not discarded"
else
  builtins.unsafeDiscardStringContext s
