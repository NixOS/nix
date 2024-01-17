let
  paths = [ ./this-file-is-definitely-not-there-7392097 "/and/neither/is/this/37293620" ];
in
  toString (builtins.concatLists (map (hash: map (builtins.hashFile hash) paths) ["md5" "sha1" "sha256" "sha512"]))

