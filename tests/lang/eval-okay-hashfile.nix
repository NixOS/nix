let
  paths = [ ./data ./binary-data ];
in
  builtins.concatLists (map (hash: map (builtins.hashFile hash) paths) ["md5" "sha1" "sha256" "sha512"])
