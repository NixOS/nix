let s = "foo ${builtins.substring 33 100 (baseNameOf ./eval-okay-context.nix)} bar";
in
  if s == "foo eval-okay-context.nix bar"
  then abort "context not discarded"
  else builtins.unsafeDiscardStringContext s

