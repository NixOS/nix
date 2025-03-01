# The flake providing default schemas.
defaultSchemasFlake:

# The flake whose contents we want to extract.
flake:

let

  # Helper functions.

  mapAttrsToList = f: attrs: map (name: f name attrs.${name}) (builtins.attrNames attrs);

in

rec {
  outputNames = builtins.attrNames flake.outputs;

  allSchemas = (flake.outputs.schemas or defaultSchemasFlake.schemas) // schemaOverrides;

  schemaOverrides = { }; # FIXME

  schemas = builtins.listToAttrs (
    builtins.concatLists (
      mapAttrsToList (
        outputName: output:
        if allSchemas ? ${outputName} then
          [
            {
              name = outputName;
              value = allSchemas.${outputName};
            }
          ]
        else
          [ ]
      ) flake.outputs
    )
  );

  outputs = flake.outputs;

  inventory = builtins.mapAttrs (
    outputName: output:
    if schemas ? ${outputName} && schemas.${outputName}.version == 1 then
      let
        schema = schemas.${outputName};
      in
      schema
      // {
        output = schema.inventory output;
      }
    else
      { unknown = true; }
  ) outputs;
}
