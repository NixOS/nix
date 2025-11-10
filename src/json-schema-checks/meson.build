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
    'stem' : 'deriving-path',
    'schema' : schema_dir / 'deriving-path-v1.yaml',
    'files' : [
      'single_opaque.json',
      'single_built.json',
      'single_built_built.json',
    ],
  },
  {
    'stem' : 'build-trace-entry',
    'schema' : schema_dir / 'build-trace-entry-v1.yaml',
    'files' : [
      'simple.json',
      'with-dependent-realisations.json',
      'with-signature.json',
    ],
  },
]

# Derivation and Derivation output
schemas += [
  # Match overall
  {
    'stem' : 'derivation',
    'schema' : schema_dir / 'derivation-v4.yaml',
    'files' : [
      'dyn-dep-derivation.json',
      'simple-derivation.json',
    ],
  },
  {
    'stem' : 'derivation',
    'schema' : schema_dir / 'derivation-v4.yaml#/$defs/output/overall',
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
  # Match exact variant
  {
    'stem' : 'derivation',
    'schema' : schema_dir / 'derivation-v4.yaml#/$defs/output/inputAddressed',
    'files' : [
      'output-inputAddressed.json',
    ],
  },
  {
    'stem' : 'derivation',
    'schema' : schema_dir / 'derivation-v4.yaml#/$defs/output/caFixed',
    'files' : [
      'output-caFixedFlat.json',
      'output-caFixedNAR.json',
      'output-caFixedText.json',
    ],
  },
  {
    'stem' : 'derivation',
    'schema' : schema_dir / 'derivation-v4.yaml#/$defs/output/caFloating',
    'files' : [
      'output-caFloating.json',
    ],
  },
  {
    'stem' : 'derivation',
    'schema' : schema_dir / 'derivation-v4.yaml#/$defs/output/deferred',
    'files' : [
      'output-deferred.json',
    ],
  },
  {
    'stem' : 'derivation',
    'schema' : schema_dir / 'derivation-v4.yaml#/$defs/output/impure',
    'files' : [
      'output-impure.json',
    ],
  },
]

# Store object info
schemas += [
  # Match overall
  {
    'stem' : 'store-object-info',
    'schema' : schema_dir / 'store-object-info-v2.yaml',
    'files' : [
      'pure.json',
      'impure.json',
      'empty_pure.json',
      'empty_impure.json',
    ],
  },
  {
    'stem' : 'nar-info',
    'schema' : schema_dir / 'store-object-info-v2.yaml',
    'files' : [
      'pure.json',
      'impure.json',
    ],
  },
  {
    'stem' : 'build-result',
    'schema' : schema_dir / 'build-result-v1.yaml',
    'files' : [
      'success.json',
      'output-rejected.json',
      'not-deterministic.json',
    ],
  },
  # Match exact variant
  {
    'stem' : 'store-object-info',
    'schema' : schema_dir / 'store-object-info-v2.yaml#/$defs/base',
    'files' : [
      'pure.json',
      'empty_pure.json',
    ],
  },
  {
    'stem' : 'store-object-info',
    'schema' : schema_dir / 'store-object-info-v2.yaml#/$defs/impure',
    'files' : [
      'impure.json',
      'empty_impure.json',
    ],
  },
  {
    'stem' : 'nar-info',
    'schema' : schema_dir / 'store-object-info-v2.yaml#/$defs/base',
    'files' : [
      'pure.json',
    ],
  },
  {
    'stem' : 'nar-info',
    'schema' : schema_dir / 'store-object-info-v2.yaml#/$defs/narInfo',
    'files' : [
      'impure.json',
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
