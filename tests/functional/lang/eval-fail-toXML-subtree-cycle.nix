let
  branch = {
    twig = branch;
  };
  root = {
    inherit branch;
  };
in
builtins.toXML root
