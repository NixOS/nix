let
  branch = {
    twig = branch;
  };
  root = {
    inherit branch;
  };
in
builtins.toJSON root
