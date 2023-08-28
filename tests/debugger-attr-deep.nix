# with import ./config.nix;

let 
  testattrs = { a = 1; b = wat 5; };
  wat = arg : arg + testattrs.b;
  foo = arg : arg2 : arg + arg2;
  
in

# foo 12 (wat 11)
testattrs


# mkDerivation {
#   name = builtins.break "simple";
#   builder = ./simple.builder.sh;
#   PATH = "";
#   goodPath = path;
# }
