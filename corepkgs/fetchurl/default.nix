{system, url, md5}: derivation {
  name = baseNameOf (toString url);
  system = system;
  builder = ./builder.sh;
  url = url;
  md5 = md5;
  id = md5;
}
