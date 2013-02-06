let
  md5 = builtins.hash "md5";
  sha256 = builtins.hash "sha256";
  strings = [ "text 1" "text 2" ];
in
  (builtins.map md5 strings) ++ (builtins.map sha256 strings)
