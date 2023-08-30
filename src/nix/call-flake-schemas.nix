flake:

let

  # Helper functions.

  mapAttrsToList = f: attrs: map (name: f name attrs.${name}) (builtins.attrNames attrs);

in

rec {
  outputNames = builtins.attrNames flake.outputs;

  allSchemas = (flake.outputs.schemas or defaultSchemas) // schemaOverrides;

  # FIXME: make this configurable
  # FIXME: a pre-cached copy of the flake-schemas needs to be built in to the nix binary
  #defaultSchemas = (builtins.getFlake "/home/eelco/Determinate/flake-schemas").schemas;
  defaultSchemas = (builtins.getFlake "github:DeterminateSystems/flake-schemas/9af39734fcd4813be17cadef98fc47285eaf7888").schemas;

  schemaOverrides = {}; # FIXME

  schemas =
    builtins.listToAttrs (builtins.concatLists (mapAttrsToList
      (outputName: output:
        if allSchemas ? ${outputName} then
          [{ name = outputName; value = allSchemas.${outputName}; }]
        else
          [ ])
      flake.outputs));

  inventory =
    builtins.mapAttrs
      (outputName: output:
        if schemas ? ${outputName} && schemas.${outputName}.version == 1
        then
          { children = schemas.${outputName}.inventory output;
            inherit (schemas.${outputName}) doc;
          }
        else
          { unknown = true; }
      )
      flake.outputs;
}
