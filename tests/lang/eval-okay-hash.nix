let
  md5 = builtins.hashString "md5";
  sha1 = builtins.hashString "sha1";
  sha256 = builtins.hashString "sha256";
  strings = [ "" "text 1" "text 2" ];
in
  (builtins.map md5 strings) ++ (builtins.map sha1 strings) ++ (builtins.map sha256 strings)
