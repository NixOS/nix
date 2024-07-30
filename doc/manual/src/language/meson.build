builtins_md = custom_target(
  command : nix_eval_for_docs + [
    '--write-to', '@OUTPUT@',
    '--expr',
    'import @INPUT0@ (builtins.fromJSON (builtins.readFile @INPUT1@))',
  ],
  input : [
    '../../generate-builtins.nix',
    language_json,
  ],
  output : 'builtins.md',
  env : nix_env_for_docs,
)
