builtins.mapAttrs (flakeName: flakeInfo:
  (getFlake flakeInfo.uri).${flakeName}.provides.packages or {})
  builtins.flakeRegistry
