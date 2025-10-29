# Run with:
#     meson test --suite json-schema
# Run with: (without shell / configure)
#     nix build .#nix-json-schema-checks

project(
  'nix-json-schema-checks',
  version : files('.version'),
  meson_version : '>= 1.1',
  license : 'LGPL-2.1-or-later',
)

fs = import('fs')

# Note: The 'jsonschema' package provides the 'jv' command
jv = find_program('jv', required : true)

# The schema directory is a committed symlink to the actual schema location
schema_dir = meson.current_source_dir() / 'schema'

# Get all example files
schemas = [
  {
    'stem' : 'hash',
    'schema' : schema_dir / 'hash-v1.yaml',
    'files' : [
      'sha256-base64.json',
      'sha256-base16.json',
      'sha256-nix32.json',
      'blake3-base64.json',
    ],
  },
  {
    'stem' : 'content-address',
    'schema' : schema_dir / 'content-address-v1.yaml',
    'files' : [
      'text.json',
      'nar.json',
    ],
  },
  {
    'stem' : 'store-path',
    'schema' : schema_dir / 'store-path-v1.yaml',
    'files' : [
      'simple.json',
    ],
  },
  {
    'stem' : 'derivation',
    'schema' : schema_dir / 'derivation-v3.yaml',
    'files' : [
      'dyn-dep-derivation.json',
      'simple-derivation.json',
    ],
  },
  {
    'stem' : 'derivation',
    'schema' : schema_dir / 'derivation-v3.yaml#/$defs/output',
    'files' : [
      'output-caFixedFlat.json',
      'output-caFixedNAR.json',
      'output-caFixedText.json',
      'output-caFloating.json',
      'output-deferred.json',
      'output-impure.json',
      'output-inputAddressed.json',
    ],
  },
  {
    'stem' : 'deriving-path',
    'schema' : schema_dir / 'deriving-path-v1.yaml',
    'files' : [
      'single_opaque.json',
      'single_built.json',
      'single_built_built.json',
    ],
  },
]

# Validate each example against the schema
foreach schema : schemas
  stem = schema['stem']
  schema_file = schema['schema']
  if '#' not in schema_file
    # Validate the schema itself against JSON Schema Draft 04
    test(
      stem + '-schema-valid',
      jv,
      args : [
        'http://json-schema.org/draft-04/schema',
        schema_file,
      ],
      suite : 'json-schema',
    )
  endif
  foreach example : schema['files']
    test(
      stem + '-example-' + fs.stem(example),
      jv,
      args : [
        schema_file,
        files(stem / example),
      ],
      suite : 'json-schema',
    )
  endforeach
endforeach
