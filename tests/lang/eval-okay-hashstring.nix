let
  strings = [ "" "text 1" "text 2" ];
in
  builtins.concatLists (map (hash: map (builtins.hashString hash) strings) ["md5" "sha1" "sha256" "sha512"])
