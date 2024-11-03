with import ./config.nix;

{
  hello = mkDerivation rec {
    name = "hello-${version}";
    version = "0.1";
    buildCommand = "touch $out";
    meta.description = "Empty file";
  };
  foo = mkDerivation rec {
    name = "foo-5";
    buildCommand = ''
      mkdir -p $out
      echo ${name} > $out/${name}
    '';
  };
  bar = mkDerivation rec {
    name = "bar-3";
    buildCommand = ''
      echo "Does not build successfully"
      exit 1
    '';
    meta.description = "broken bar";
  };
}
