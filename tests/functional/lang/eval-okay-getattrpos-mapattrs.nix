let
  attrs = {
    a = { };
  };

  mappedAttrs = builtins.mapAttrs (key: value: value) attrs;
in builtins.unsafeGetAttrPos "a" mappedAttrs
