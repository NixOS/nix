url: subdir: with builtins;
let
  hasPrefix  = pref: str: substring 0 (stringLength pref) str == pref;
in
builtins.fetchGit {
  inherit url;
  filter = path: type:
    (hasPrefix (subdir + "/") path) ||
               (type == "directory" && path == subdir);
}
