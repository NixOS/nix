with builtins;
with import ./utils.nix;

builtins:

concatStrings (map
  (name:
    let builtin = builtins.${name}; in
    "  - `builtins.${name}` " + concatStringsSep " " (map (s: "*${s}*") builtin.args)
    + "  \n\n"
    + concatStrings (map (s: "    ${s}\n") (splitLines builtin.doc)) + "\n\n"
  )
  (attrNames builtins))

