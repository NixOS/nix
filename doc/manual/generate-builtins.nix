with builtins;
with import ./utils.nix;

builtins:

concatStrings (map
  (name:
    let builtin = builtins.${name}; in
    "<dt id=\"${name}\"><a href=\"#${name}\"><code>${name} "
    + concatStringsSep " " (map (s: "<var>${s}</var>") builtin.args)
    + "</code></a></dt>"
    + "<dd>\n\n"
    + builtin.doc
    + "\n\n</dd>"
  )
  (attrNames builtins))
