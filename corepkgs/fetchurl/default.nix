{system, url, md5}:

derivation {
  name = baseNameOf (toString url);
  builder = ./builder.sh;
  id = md5;
  inherit system url md5;
}
