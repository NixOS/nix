# Tests in: ../../../../src/json-schema-checks

fs = import('fs')

# Find json-schema-for-humans if available
json_schema_for_humans = find_program('generate-schema-doc', required : false)

# Configuration for json-schema-for-humans
json_schema_config = files('json-schema-for-humans-config.yaml')

schemas = [
  'hash-v1',
  'content-address-v1',
  'store-path-v1',
  'store-object-info-v2',
  'derivation-v4',
  'deriving-path-v1',
  'build-trace-entry-v1',
  'build-result-v1',
]

schema_files = files()
foreach schema_name : schemas
  schema_files += files('schema' / schema_name + '.yaml')
endforeach


schema_outputs = []
foreach schema_name : schemas
  schema_outputs += schema_name + '.md'
endforeach

json_schema_generated_files = []

# Generate markdown documentation from JSON schema
# Note: output must be just a filename, not a path
gen_file = custom_target(
  schema_name + '-schema-docs.tmp',
  command : [
    json_schema_for_humans,
    '--config-file',
    json_schema_config,
    meson.current_source_dir() / 'schema',
    meson.current_build_dir(),
  ],
  input : schema_files + [
    json_schema_config,
  ],
  output : schema_outputs,
  capture : false,
  build_by_default : true,
)

idx = 0
if json_schema_for_humans.found()
  foreach schema_name : schemas
    #schema_file = 'schema' / schema_name + '.yaml'

    # There is one so-so hack, and one horrible hack being done here.
    sedded_file = custom_target(
      schema_name + '-schema-docs',
      command : [
        'sed',
        '-f',
        # Out of line to avoid https://github.com/mesonbuild/meson/issues/1564
        files('fixup-json-schema-generated-doc.sed'),
        '@INPUT@',
      ],
      capture : true,
      input : gen_file[idx],
      output : schema_name + '-fixed.md',
    )
    idx += 1
    json_schema_generated_files += [ sedded_file ]
  endforeach
else
  warning(
    'json-schema-for-humans not found, skipping JSON schema documentation generation',
  )
endif
