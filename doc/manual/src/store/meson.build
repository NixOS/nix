types_dir = custom_target(
  command : nix_eval_for_docs + [
    '--write-to', '@OUTPUT@',
    '--expr',
    'import @INPUT0@ (builtins.fromJSON (builtins.readFile @INPUT1@)).stores',
  ],
  input : [
    '../../generate-store-types.nix',
    nix3_cli_json,
  ],
  output : 'types',
  env : nix_env_for_docs,
)
