with builtins;
with import ./utils.nix;

builtins:

concatStrings (map
  (name:
    let builtin = builtins.${name}; in
    "<dt><code>${name} "
    + concatStringsSep " " (map (s: "<var>${s}</var>") builtin.args)
    + "</code></dt>"
    + "<dd>\n\n"
    + builtin.doc
    + "\n\n</dd>"
  )
  (attrNames builtins))
