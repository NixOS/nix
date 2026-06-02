# The flake providing default schemas.
defaultSchemasFlake:

# The flake whose contents we want to extract.
flake:

let

  # Helper functions.

  mapAttrsToList = f: attrs: map (name: f name attrs.${name}) (builtins.attrNames attrs);

  outputNames = builtins.attrNames flake.outputs;

  schemas = flake.outputs.schemas or defaultSchemasFlake.exportedSchemas;

in

{
  outputs = flake.outputs;

  inventory = builtins.mapAttrs (
    outputName: _:
    if schemas ? ${outputName} && schemas.${outputName}.version == 1 then
      schemas.${outputName}
      // (
        if flake.outputs ? ${outputName} then
          {
            output = schemas.${outputName}.inventory flake.outputs.${outputName};
          }
        else
          {
          }
      )
    else
      { unknown = true; }
  ) (schemas // flake.outputs);
}
