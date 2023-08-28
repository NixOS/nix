# with import ./config.nix;

let 
  testattrs = { a = 1; b = 2; };
  wat = arg : arg + testattrs.c;
  foo = arg : arg2 : arg + arg2;
  
in

foo 12 (wat 11)


# mkDerivation {
#   name = builtins.break "simple";
#   builder = ./simple.builder.sh;
#   PATH = "";
#   goodPath = path;
# }
